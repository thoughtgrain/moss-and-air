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
enum { F_PCT, F_BIPCT, F_ST, F_NOTE, F_MS, F_NUM, F_DB, F_ENUM, F_CT, F_HZ, F_TIME };   /* how a value prints */

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
static const char *const N_OSC[5] = {"SIN", "TRI", "SQR", "SAW", "PLS"};
static const char *const N_HOLD[5] = {"OFF", "1/32", "1/16", "1/8", "1/4"};
#include "bryo_reels.h"              /* the factory reels' names (tools/gen_reels.py) */
#define USLOT_N 6u                   /* user reels: your sounds in flash (reel.c) */
static char uslot_name[USLOT_N][6];  /* their names (reel.c uslot_names) */
static const char *const N_REEL[1u + NREEL + USLOT_N] = {"TAPE", REEL_NAMES_INIT, uslot_name[0], uslot_name[1],
                                                         uslot_name[2], uslot_name[3], uslot_name[4], uslot_name[5]};

#define NPK 16u                  /* knobs a device or engine can have: up to four pages of four */

static const char *const DEV_NAME[NDEV] = {"TAPE", "GRAIN", "RESONATOR", "COLOR", "SPACE"};

/* Page 1 of each device is the PRD's four knobs. The pages after it hold what the sound needs that the PRD left
 * without a knob: wet levels, the switches the black keys flip, the shapes. The names follow the Torso S-4's
 * devices where Bryo's does the same job (docs/s4-alignment.md): TAPE's SOS is DUB, MOSAIC's SPRAY is SPRY, and so
 * on; the PRD's knob positions stay. A device's pages run until one whose first label is empty. */
static const char *const N_PATN[4] = {"EVEN", "SWNG", "CLST", "RND"};
static const char *const N_SCAN[4] = {"TAPE", "STR", "POS", "DLY"};
static const char *const N_GSCAL[5] = {"OFF", "CHR", "MAJ", "MIN", "PEN"};
static const char *const N_RSCAL[4] = {"HARM", "MAJ", "MIN", "PEN"};
static const char *const N_SLOP[3] = {"LP", "BP", "HP"};
static const char *const N_CMOD[3] = {"BIT", "RATE", "BOTH"};
static const pdesc_t DEV_P[NDEV][NPK] = {
    {   /* TAPE (the source when it's TAPE: SYNTH and POLY have their own tables) */
        {"STRT", 0, 100, 0, F_PCT}, {"LEN", 1, 100, 100, F_PCT},
        {"SPD", -200, 200, 100, F_BIPCT}, {"DUB", -100, 100, 0, F_BIPCT},
        /* 2: the crossfade at the loop's seam, reverse and half speed (OP5 and OP6 flip them too), the record gain */
        {"XFAD", 0, 100, 10, F_MS}, {"REV", 0, 1, 0, F_ENUM, N_OFFON},
        {"HALF", 0, 1, 0, F_ENUM, N_OFFON}, {"GAIN", -12, 12, 0, F_DB},
        /* 3: what the track plays: its own tape, a factory reel or one of your reels (in flash; REC copies it onto
         * the tape); ROTATE: where in the loop playing (and slice 1) starts against the transport */
        {"REEL", 0, NREEL + USLOT_N, 0, F_ENUM, N_REEL}, {"ROTA", 0, 99, 0, F_PCT}, {""}, {""}},
    {   /* GRAIN (the S-4's MOSAIC) */
        {"SIZE", 5, 500, 80, F_MS}, {"RATE", 0, 100, 40, F_PCT},
        {"PTCH", -24, 24, 0, F_ST}, {"SPRD", 0, 100, 30, F_PCT},
        /* 2: dry/wet, SPRAY (the jitter of where grains read), the window's contour (square .. smooth), the chance
         * a grain plays backwards */
        {"WET", 0, 100, 0, F_PCT}, {"SPRY", 0, 100, 20, F_PCT},   /* (WET 0: off until it's turned up) */
        {"CONT", 0, 100, 50, F_PCT}, {"REV", 0, 100, 0, F_PCT},
        /* 3: WARP: how fast the read point moves (0 holds it, - backwards); the order grains
         * fire in; their pitch held to a scale; a random pitch per grain, up to +-PRND semitones */
        {"WARP", -200, 200, 100, F_BIPCT}, {"PATN", 0, 3, 3, F_ENUM, N_PATN},
        {"SCAL", 0, 4, 0, F_ENUM, N_GSCAL}, {"PRND", 0, 12, 0, F_ST},
        /* 4: what grains read and where (grain.c): the track's TAPE, or the live buffer of the last bars, read
         * stretching behind the write head (STR), at a spot (POS) or a delay behind it (DLY); OFST: POS's spot, DLY's
         * delay, as a share of the buffer; FDBK: how much of the buffer stays as new sound goes in (the S-4's) */
        {"SCAN", 0, 3, 1, F_ENUM, N_SCAN}, {"OFST", 0, 100, 25, F_PCT},
        {"FDBK", 0, 100, 0, F_PCT}, {""}},
    {   /* RESONATOR (the S-4's RING) */
        {"PTCH", 33, 81, 45, F_NOTE}, {"DEC", 0, 100, 60, F_PCT},
        {"TONE", 0, 100, 60, F_PCT}, {"WET", 0, 100, 0, F_PCT},
        /* 2: a filter before the strings (cutoff, resonance, its slope: low-, band- or high-pass), and the
         * strings' tuning: the root's harmonics, or a scale's chord tones from the root */
        {"CUT", 0, 127, 127, F_HZ}, {"RES", 0, 100, 0, F_PCT},
        {"SLOP", 0, 2, 0, F_ENUM, N_SLOP}, {"SCAL", 0, 3, 0, F_ENUM, N_RSCAL}},
    {   /* COLOR (the S-4's DEFORM) */
        {"DRIV", 0, 100, 0, F_PCT}, {"CRSH", 0, 100, 0, F_PCT},
        {"NOIS", 0, 100, 0, F_PCT}, {"TILT", -100, 100, 0, F_BIPCT},
        /* 2: the noise's decay after the sound that opens it, the noise's tone, what CRUSH takes (bits, the sample
         * rate, both), dry/wet */
        {"NDEC", 0, 127, 40, F_TIME}, {"NTON", -100, 100, 0, F_BIPCT},
        {"CMOD", 0, 2, 0, F_ENUM, N_CMOD}, {"WET", 0, 100, 100, F_PCT},
        /* 3: the output level (drive adds loudness) */
        {"LVL", -24, 6, 0, F_DB}, {""}, {""}, {""}},
    {   /* SPACE (the S-4's VAST) */
        {"TIME", 10, 370, 250, F_MS}, {"FDBK", 0, 100, 30, F_PCT},
        {"SIZE", 0, 100, 50, F_PCT}, {"DEC", 0, 100, 40, F_PCT},
        /* 2: the delay's and the reverb's levels, a tone on both (- low-pass, + high-pass, on the feedback and the
         * tail), the stereo spread */
        {"DLY", 0, 100, 30, F_PCT}, {"VERB", 0, 100, 30, F_PCT},
        {"TONE", -100, 100, 0, F_BIPCT}, {"SPRD", 0, 100, 100, F_PCT},
        /* 3: the reverb's pre-delay */
        {"PRE", 0, 200, 20, F_MS}, {""}, {""}, {""}},
};

/* The source engines: what starts a track's chain (docs/bryo-architecture.md, "Source engines"). TAPE's knobs are
 * DEV_P[DEV_SRC]; each other source has its own table, and each track keeps every source's values, so switching
 * back and forth loses nothing. */
enum { SRC_TAPE, SRC_SYNTH, SRC_POLY, NSRC };
static const char *const SRC_NAME[NSRC] = {"TAPE", "SYNTH", "POLY"};

/* SYNTH (synth.c): a small subtractive voice from Felucca's ANALOG engine, up to three per track. Four pages, the
 * way a synth's panel reads left to right: the oscillators, the filter, the envelope (one ADSR for the level and,
 * by ENV, the cutoff), then how the keys play it. */
enum { SY_WAVE, SY_DTUN, SY_MIX, SY_NOIS, SY_CUT, SY_RES, SY_ENV, SY_KTRK, SY_ATK, SY_DEC, SY_SUS, SY_REL, SY_VOIC,
       SY_GLID, SY_DRV, SY_TUNE };
#define SYN_NV 3u                /* voices per track */
static const pdesc_t SYN_P[NPK] = {
    /* OSC: two oscillators of the same wave, the second DTUN cents up and MIX of the blend; white noise */
    {"WAVE", 0, 4, 3, F_ENUM, N_OSC}, {"DTUN", 0, 100, 8, F_CT}, {"MIX", 0, 100, 50, F_PCT}, {"NOIS", 0, 100, 0, F_PCT},
    /* FILTER: a resonant low-pass; ENV opens (or, below 0, closes) it with the envelope; KTRK follows the keys */
    {"CUT", 0, 127, 72, F_HZ}, {"RES", 0, 100, 25, F_PCT}, {"ENV", -100, 100, 45, F_BIPCT}, {"KTRK", 0, 100, 50, F_PCT},
    /* AMP: the envelope's times (1 ms .. 10 s) and its sustain level */
    {"ATK", 0, 127, 0, F_TIME}, {"DEC", 0, 127, 60, F_TIME}, {"SUS", 0, 100, 60, F_PCT}, {"REL", 0, 127, 45, F_TIME},
    /* VOICE: how many keys sound at once (1: one voice, legato), the glide between notes, drive before the filter,
     * the tuning in semitones */
    {"VOIC", 1, SYN_NV, SYN_NV, F_NUM}, {"GLID", 0, 127, 0, F_TIME}, {"DRV", 0, 100, 0, F_PCT},
    {"TUNE", -24, 24, 0, F_ST}};

/* POLY (poly.c): a sound played at the keys' pitches, up to four at once, each through its own envelope and filter.
 * The sound is any reel choice: a factory reel, one of yours, or this track's own tape (record into it, then play it
 * across the keys). White key 1 at OCT 3 plays it as it was recorded. */
enum { PL_REEL, PL_STRT, PL_TUNE, PL_VOIC, PL_ATK, PL_DEC, PL_SUS, PL_REL, PL_CUT, PL_RES, PL_TYPE, PL_ENV };
#define POL_NV 4u                /* voices per track */
static const pdesc_t POL_P[NPK] = {
    /* SAMPLE: which sound, where in it a note starts, the tuning, how many notes at once */
    {"REEL", 0, NREEL + USLOT_N, 1, F_ENUM, N_REEL}, {"STRT", 0, 99, 0, F_PCT}, {"TUNE", -24, 24, 0, F_ST},
    {"VOIC", 1, POL_NV, POL_NV, F_NUM},
    /* ENV: each voice's envelope */
    {"ATK", 0, 127, 0, F_TIME}, {"DEC", 0, 127, 60, F_TIME}, {"SUS", 0, 100, 100, F_PCT}, {"REL", 0, 127, 40, F_TIME},
    /* FILTER: each voice's state-variable filter, and how far its envelope moves the cutoff */
    {"CUT", 0, 127, 127, F_HZ}, {"RES", 0, 100, 0, F_PCT}, {"TYPE", 0, 2, 0, F_ENUM, N_SLOP},
    {"ENV", -100, 100, 0, F_BIPCT},
    {""}, {""}, {""}, {""}};

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
    uint8_t src;                 /* the source engine (SRC_*): the main loop writes it, the ISR follows */
    uint8_t recin;               /* what REC records onto the tape (RIN_*: the routing view) */
    int16_t syn[NPK];            /* SYNTH's knobs (TAPE's are dev[DEV_SRC]) */
    int16_t pol[NPK];            /* POLY's */
} track_params_t;
static track_params_t tp[NTRK];

/* What a track's REC records (the routing view: ALGORITHM, KNOB 1..4 a track each). AUTO is what REC always did:
 * on a TAPE track the other tracks' mix, on a SYNTH or POLY track its own source (play, then slice it). OTHR: the
 * others' mix whatever the source; T1..T4: that one track, after its effects (a track's own number reads SELF: it
 * records itself back onto its tape, DUB setting how much of the old pass stays). */
enum { RIN_AUTO, RIN_OTHR, RIN_T1 };
#define NRIN (RIN_T1 + NTRK)
static const char *const N_RIN[NTRK][NRIN] = {
    {"AUTO", "OTHR", "SELF", "T2", "T3", "T4"}, {"AUTO", "OTHR", "T1", "SELF", "T3", "T4"},
    {"AUTO", "OTHR", "T1", "T2", "SELF", "T4"}, {"AUTO", "OTHR", "T1", "T2", "T3", "SELF"}};
static const pdesc_t RIN_P[NTRK] = {
    {"T1", 0, NRIN - 1, RIN_AUTO, F_ENUM, N_RIN[0]}, {"T2", 0, NRIN - 1, RIN_AUTO, F_ENUM, N_RIN[1]},
    {"T3", 0, NRIN - 1, RIN_AUTO, F_ENUM, N_RIN[2]}, {"T4", 0, NRIN - 1, RIN_AUTO, F_ENUM, N_RIN[3]}};

/* device d of track t as the pages see it: the source's knobs are the chosen source's */
static const pdesc_t *dev_p(uint32_t t, uint32_t d)
{
    if (d != DEV_SRC || tp[t].src == SRC_TAPE)
        return DEV_P[d];
    return tp[t].src == SRC_SYNTH ? SYN_P : POL_P;
}
static int16_t *dev_v(uint32_t t, uint32_t d)
{
    if (d != DEV_SRC || tp[t].src == SRC_TAPE)
        return tp[t].dev[d];
    return tp[t].src == SRC_SYNTH ? tp[t].syn : tp[t].pol;
}
static const char *dev_name(uint32_t t, uint32_t d) { return d == DEV_SRC ? SRC_NAME[tp[t].src % NSRC] : DEV_NAME[d]; }

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
        tp[t].src = SRC_TAPE;
        tp[t].recin = RIN_AUTO;
        for (k = 0; k < NPK; k++) {
            tp[t].syn[k] = SYN_P[k].def;
            tp[t].pol[k] = pdesc_empty(&POL_P[k]) ? 0 : POL_P[k].def;
        }
        tp[t].pol[PL_REEL] = (int16_t)(t < NREEL ? t + 1u : 1u);       /* POLY starts on the track's reel too */
        tp[t].dev[DEV_SRC][8] = (int16_t)(t < NREEL ? t + 1u : 0u);   /* track n plays reel n to start */
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
    case F_CT:
        fmt_int(val, v);
        *unit = "ct";
        break;
    case F_HZ:                                          /* a cutoff (0..127 on CUTOFF_HZ): 820Hz, 2.4k, 12k */
    case F_TIME: {                                      /* a time (0..127 on TIME_MS_X10): 35ms, 1.2s */
        uint32_t x = d->fmt == F_HZ ? CUTOFF_HZ[v & 127] : (TIME_MS_X10[v & 127] + 5u) / 10u;
        if (x < 1000u) {
            fmt_int(val, (int32_t)x);
            *unit = d->fmt == F_HZ ? "Hz" : "ms";
        } else {
            fmt_int(val, (int32_t)(x / 1000u));
            if (x < 10000u) {
                str_cpy(val + str_len(val), ".", 2);
                fmt_int(val + str_len(val), (int32_t)(x % 1000u / 100u));
            }
            *unit = d->fmt == F_HZ ? "k" : "s";
        }
        break;
    }
    default:
        fmt_int(val, v);
        break;
    }
}

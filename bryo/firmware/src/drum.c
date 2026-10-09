/* SPDX-License-Identifier: GPL-3.0-only */
/* DRUM: a drum machine as a track's source. Sixteen synthesized instruments on the sixteen white keys, played from
 * a pattern of 2..4 bars of sixteenth steps; the kit is CR-78-inspired (its instrument list and its soft, round
 * character), the sounds and the rhythms are Bryo's own. The voices are drum_voice.c's.
 *
 * The kit (DRM_INST): what each key plays, as a voice type and the offsets from that type's designed sound. The
 * bongos are the conga's voice tuned up; the metal beat is a closed hat made bright and short. A hi-hat (closed)
 * and the metal beat choke the open hat, the way one pedal would. */

#define DRM_NINST 16

enum {
    DI_BD, DI_SD, DI_CP, DI_RS, DI_LC, DI_LB, DI_HB, DI_CL,
    DI_CB, DI_MA, DI_TB, DI_GU, DI_HH, DI_OH, DI_MB, DI_CY
};

static const struct {
    char code[3];                                        /* on screen */
    uint8_t type;                                        /* DVT_* */
    int16_t tune;                                        /* 1/16 semitone from the type's designed pitch */
    uint8_t decay, tone, extra;                          /* 0..127 */
    uint8_t level;                                       /* 0..127 (100: the voice's designed level) */
    uint16_t chokes;                                     /* the instruments a hit of this one silences */
} DRM_INST[DRM_NINST] = {
    {"BD", DVT_ROUND,    0, 40, 40, 30, 110, 0},         /* bass drum: the round kick, softer and shorter */
    {"SD", DVT_SNARE,   32, 40, 90, 70, 100, 0},         /* snare: +2 semitones, bright, snappy */
    {"CP", DVT_CLAP,     0, 64, 64, 64, 100, 0},         /* clap */
    {"RS", DVT_RIM,      0, 64, 64, 64, 100, 0},         /* rim shot */
    {"LC", DVT_CONGA, -112, 64, 64, 40, 100, 0},         /* low conga: -7 semitones */
    {"LB", DVT_CONGA,   26, 52, 70, 30, 100, 0},         /* low bongo */
    {"HB", DVT_CONGA,  128, 44, 80, 30, 100, 0},         /* high bongo: +8 semitones */
    {"CL", DVT_CLAVE,    0, 64, 64, 64, 100, 0},         /* claves */
    {"CB", DVT_BELL,     0, 64, 64, 64, 100, 0},         /* cowbell */
    {"MA", DVT_MARACA,   0, 64, 64, 64, 100, 0},         /* maracas */
    {"TB", DVT_TAMB,     0, 64, 64, 64, 100, 0},         /* tambourine */
    {"GU", DVT_GUIRO,    0, 64, 64, 64, 100, 0},         /* guiro */
    {"HH", DVT_HATC,     0, 64, 64, 64, 100, 1u << DI_OH},   /* hi-hat */
    {"OH", DVT_HATO,     0, 64, 64, 64, 100, 0},         /* open hi-hat */
    {"MB", DVT_HATC,     0, 30, 110, 110, 100, 1u << DI_OH}, /* metal beat: a hat, bright and short */
    {"CY", DVT_CYM,      0, 64, 64, 64, 90, 0},          /* cymbal */
};

/* an instrument's voice parameters as designed (the kit's knobs and the instrument's own are applied over this) */
static void drm_inst_param(uint32_t i, dv_param_t *p)
{
    i = i < DRM_NINST ? i : 0u;
    dv_default(p, DRM_INST[i].type);
    p->tune = DRM_INST[i].tune;
    p->decay = DRM_INST[i].decay;
    p->tone = DRM_INST[i].tone;
    p->extra = DRM_INST[i].extra;
    p->level = DRM_INST[i].level;
}

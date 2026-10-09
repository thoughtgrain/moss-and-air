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

/* code, TONE: p16 ratio mix2 square bend bend_ms body_ms tone_lv,
 *       NOISE: filt cut q16 mset mp16 metal rise10 hit_ms hit_lv bursts gap10 late tail_ms tail_lv,
 *       drive gain chokes */
static const dk_patch_t DRM_KIT[DRM_NINST] = {
    /* bass drum: a 58 Hz sine that drops 2.5 semitones in its first few ms, a soft 2nd harmonic, a felt thud */
    {"BD", 543, 8192, 22, 0, 40, 6, 110, 127,
     DK_LOW, 1331, 11, DK_M_NONE, 0, 0, 0, 2, 25, 0, 0, 0, 2, 0, 30, 3779, 0},
    /* snare: two shell tones (190 Hz, x1.72) under a band of noise at 4.5 kHz, a quick snap on top */
    {"SD", 871, 7045, 70, 0, 16, 5, 45, 75,
     DK_BAND, 1748, 11, DK_M_NONE, 0, 0, 0, 4, 70, 0, 0, 0, 95, 110, 10, 2644, 0},
    /* clap: four hands 9 ms apart through a band at 1.2 kHz, then the room */
    {"CP", 0, 0, 0, 0, 0, 0, 0, 0,
     DK_BAND, 1380, 19, DK_M_NONE, 0, 0, 0, 5, 127, 3, 90, 1, 70, 55, 0, 3216, 0},
    /* rim shot: 480 Hz and an inharmonic partial, very short, driven hard */
    {"RS", 1128, 10732, 90, 0, 0, 0, 7, 127,
     DK_HIGH, 1777, 11, DK_M_NONE, 0, 0, 0, 2, 30, 0, 0, 0, 2, 0, 80, 6643, 0},
    /* low conga: 205 Hz, a 1.5-semitone fall, a slap of bright noise */
    {"LC", 892, 0, 0, 0, 24, 20, 170, 127,
     DK_HIGH, 1585, 11, DK_M_NONE, 0, 0, 0, 3, 25, 0, 0, 0, 2, 0, 20, 3143, 0},
    /* low bongo: 330 Hz */
    {"LB", 1024, 0, 0, 0, 24, 15, 120, 127,
     DK_HIGH, 1636, 11, DK_M_NONE, 0, 0, 0, 3, 25, 0, 0, 0, 2, 0, 20, 3143, 0},
    /* high bongo: 470 Hz */
    {"HB", 1122, 0, 0, 0, 20, 12, 90, 127,
     DK_HIGH, 1636, 11, DK_M_NONE, 0, 0, 0, 3, 25, 0, 0, 0, 2, 0, 20, 3143, 0},
    /* claves: 2.5 kHz, a click of wood */
    {"CL", 1585, 0, 0, 0, 0, 0, 10, 127,
     DK_OFF, 0, 0, DK_M_NONE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 3001, 0},
    /* cowbell: two squares (560 Hz, x1.49) through a band at 1.3 kHz, a struck edge and a ring */
    {"CB", 0, 0, 0, 0, 0, 0, 0, 0,
     DK_BAND, 1404, 24, DK_M_PAIR, 1171, 127, 0, 8, 80, 0, 0, 0, 110, 70, 0, 1522, 0},
    /* maracas: noise in a wide band at 7 kHz that swells in over 2 ms and falls away */
    {"MA", 0, 0, 0, 0, 0, 0, 0, 0,
     DK_BAND, 1870, 10, DK_M_NONE, 0, 0, 20, 1, 0, 0, 0, 0, 35, 127, 0, 3329, 0},
    /* tambourine: noise and a little metal at 8.5 kHz, the strike and two jingles 16 ms apart */
    {"TB", 0, 0, 0, 0, 0, 0, 0, 0,
     DK_BAND, 1924, 13, DK_M_CLUSTER, 1236, 50, 5, 9, 90, 2, 160, 0, 140, 70, 0, 3446, 0},
    /* guiro: 23 teeth 4.2 ms apart through a narrow band at 2.4 kHz */
    {"GU", 0, 0, 0, 0, 0, 0, 0, 0,
     DK_BAND, 1574, 20, DK_M_NONE, 0, 0, 0, 2, 127, 22, 42, 0, 2, 0, 0, 3002, 0},
    /* hi-hat: noise and a little metal above 7 kHz, short */
    {"HH", 0, 0, 0, 0, 0, 0, 0, 0,
     DK_HIGH, 1870, 11, DK_M_CLUSTER, 1236, 40, 3, 0, 0, 0, 0, 0, 28, 127, 0, 2330, 1u << DI_OH},
    /* open hi-hat: the same above 6 kHz, long */
    {"OH", 0, 0, 0, 0, 0, 0, 0, 0,
     DK_HIGH, 1828, 11, DK_M_CLUSTER, 1236, 40, 3, 0, 0, 0, 0, 0, 260, 110, 0, 2469, 0},
    /* metal beat: mostly metal, through a band at 9 kHz, a hard short tick */
    {"MB", 0, 0, 0, 0, 0, 0, 0, 0,
     DK_BAND, 1940, 16, DK_M_CLUSTER, 1300, 110, 2, 3, 70, 0, 0, 0, 18, 110, 20, 4924, 1u << DI_OH},
    /* cymbal: noise and metal in a wide band at 6 kHz, a splash and a long wash */
    {"CY", 0, 0, 0, 0, 0, 0, 0, 0,
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
}

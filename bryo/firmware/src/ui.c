/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: the screen. Four regions, each redrawn only when what it shows changes (a signature per region), all on
 * the dot grid of ui_px.c (2 x 2 px dots, one ink; dot rows in brackets):
 *
 *   y   0..25   header  [T1] COLOR, [M1] WAVE in bold, the inverted tempo bar with REC and the transport  (0..12)
 *   y  26..123  strip   KNOB 1..4 as pictograms that show their values, the labels and the values (PRD 5)  (13..61)
 *   y 124..215  viz     what the page does, drawn from its values (ui_viz.c); the mixer's meters; messages  (62..107)
 *   y 216..239  footer  what the keys do now, the track and its octave                                      (108..119)
 *
 * The tracks themselves live in the mixer (GLO): hold GLO and press white key 1..4 to pick one. The pages don't
 * repeat them; the header and the footer name the focused track.
 *
 * Main loop only. The audio ISR never touches the UI; the UI reads the ISR's meters (track_rt[].peak) as plain
 * words. Colour is never the only cue: a modulator slot's colour always comes with its number. */

#define UI_HEAD_H 26
#define UI_STRIP_Y 26
#define UI_STRIP_H 98
#define UI_VIZ_Y 124
#define UI_FOOT_Y 216
#define UI_FOOT_H 24
#define UI_MSG_FRAMES 70             /* a message holds the visualization panel ~1 s (~66 frames/s) */

enum { FOCUS_DEV, FOCUS_SLOT };      /* what the strip shows: a device of the track, or a modulator slot */
enum { VIEW_PAGE, VIEW_MIXER };      /* VIEW_MIXER: GLO, the four track levels on the knobs, the tracks below */

static struct {
    uint8_t kind;                    /* FOCUS_DEV / FOCUS_SLOT */
    uint8_t dev;                     /* DEV_* when FOCUS_DEV */
    uint8_t slot;                    /* 0..3 when FOCUS_SLOT */
    uint8_t view;
    uint8_t hot, hot_t;              /* the knob just turned (its dial and value in the accent), frames left */
    uint8_t last;                    /* the page's most recently turned knob (0..3; 0xFF none since the page opened):
                                      * the visualization draws its part in the accent and names it */
    uint8_t uboot;                   /* UPDATE MODE countdown, seconds left (main.c), 0 = none */
    uint8_t glo_held;                /* GLO held: the mixer is up, white keys 1..4 pick the track */
    uint8_t glo_used;                /* .. and something was done while it was held (so letting go closes it) */
    uint8_t glo_latched;             /* the mixer stays up (GLO tapped) */
    uint32_t glo_t0;                 /* when GLO went down */
    uint8_t rec;                     /* REC armed, bit per track (TAPE recording arrives in phase 2) */
    uint8_t force;                   /* redraw everything next frame */
    uint8_t msg_t;
    char msg[40];
    uint32_t sig_head, sig_strip, sig_viz, sig_foot;
} ui;

/* modulator slot colours (PRD 5: 1 cyan, 2 amber, 3 green, 4 magenta), RGB565, chosen to read on every dark
 * palette; each is always drawn with its slot number next to it */
static const uint16_t SLOT_COLOR[NSLOT] = {0x3E7Du, 0xFD20u, 0x5F0Bu, 0xE31Fu};

static void draw_viz(void);           /* ui_viz.c */

static void ui_message(const char *s)
{
    str_cpy(ui.msg, s, sizeof ui.msg);
    ui.msg_t = UI_MSG_FRAMES;
}

static void ui_redraw(void) { ui.force = 1; }

static void ui_init(void)
{
    ui.kind = FOCUS_DEV;
    ui.dev = DEV_SRC;
    ui.last = 0xFF;
    ui.force = 1;
}

/* the four values the knobs edit right now, and their descriptors */
static const pdesc_t *ui_page(uint32_t k, int16_t **vp)
{
    track_params_t *p = &tp[sys.sel];
    static const pdesc_t LEVEL[NTRK] = {{"T1", 0, 127, 100, F_NUM}, {"T2", 0, 127, 100, F_NUM},
                                        {"T3", 0, 127, 100, F_NUM}, {"T4", 0, 127, 100, F_NUM}};
    static int16_t lv[NTRK];
    if (ui.view == VIEW_MIXER) {
        lv[k] = track[k].level;
        *vp = &lv[k];
        return &LEVEL[k];
    }
    if (ui.kind == FOCUS_SLOT) {
        *vp = &p->mod[ui.slot][k];
        return &ME_P[p->engine[ui.slot]][k];
    }
    *vp = &p->dev[ui.dev][k];
    return &DEV_P[ui.dev][k];
}

static uint32_t hash_str(uint32_t h, const char *s)
{
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

/* ------------------------------------------------------------ header --- */
/* [T1] RESONATOR  [rec play metronome 120]: the box names the track (a modulator page: the slot, M1..M4, with its
 * colour under the box), the title the page in bold, and the tempo bar (inverted, like a groovebox's) fills the rest. */
static const char *const PX_METRO[7] = {"..#..", ".#..#", ".#.#.", "#.#.#", "##..#", "#...#", "#####"};

static void draw_head(void)
{
    char ti[16], box[4] = {'T', (char)('1' + sys.sel), 0, 0}, bpm[8];
    uint32_t sig;
    int slot = ui.kind == FOCUS_SLOT && ui.view == VIEW_PAGE;
    if (ui.view == VIEW_MIXER)
        str_cpy(ti, "MIXER", sizeof ti);
    else
        str_cpy(ti, slot ? ME_NAME[tp[sys.sel].engine[ui.slot]] : DEV_NAME[ui.dev], sizeof ti);
    if (slot) {
        box[0] = 'M';
        box[1] = (char)('1' + ui.slot);
    }
    fmt_int(bpm, sys.bpm);
    sig = hash_str(hash_str(hash_str(2166136261u, ti), bpm), box) + sys.playing * 7u + ((ui.rec >> sys.sel) & 1u) * 131u +
          ux.theme * 3u;
    if (!ui.force && sig == ui.sig_head)
        return;
    ui.sig_head = sig;
    px_colors();
    cv_begin(240, UI_HEAD_H, T_BG);
    {
        int32_t bw = px_text_w(PXF_5, box) + 4, x = bw + 2, bar;
        px_frame(0, 1, bw, 11, px_ink, 1);
        px_text(2, 3, PXF_5, box, px_ink);
        if (slot)
            px_box(0, 12, bw, 1, SLOT_COLOR[ui.slot]);     /* the slot's colour, under its number */
        x = px_text(x, 3, PXF_5B, ti, px_ink);
        bar = x + 1;
        px_box(bar, 1, 120 - bar, 11, px_ink);
        x = 118 - px_text_w(PXF_5, bpm);
        px_text(x, 3, PXF_5, bpm, px_bg);
        if (x - 7 - bar >= 13)                             /* the metronome, when the title leaves room */
            px_art(x -= 7, 3, PX_METRO, 7, px_bg);
        x -= 6;
        if (sys.playing) {                                 /* a triangle playing, a square stopped */
            int32_t r;
            for (r = 0; r < 7; r++)
                px_box(x, 3 + r, 4 - (r < 4 ? 3 - r : r - 3), 1, px_bg);
        } else {
            px_box(x, 4, 4, 5, px_bg);
        }
        if ((ui.rec >> sys.sel) & 1u) {                    /* REC armed: a round dot in a ring of the background, so */
            x -= 7;                                        /* it shows where REC's colour is the ink (GREY, MONO) */
            px_box(x, 4, 5, 5, px_bg);
            px_dot(x, 4, px_ink);
            px_dot(x + 4, 4, px_ink);
            px_dot(x, 8, px_ink);
            px_dot(x + 4, 8, px_ink);
            px_box(x + 1, 5, 3, 3, T_REC);
        }
    }
    cv_blit(0, 0);
}

/* ------------------------------------------------------------- strip --- */
/* four cells of 30 dots: the knob's pictogram (it shows the value), its label, the value. The page's last-turned
 * knob has its label inverted. */
static void draw_strip(void)
{
    uint32_t k, sig = 2166136261u + ux.theme * 3u;
    for (k = 0; k < 4u; k++) {
        int16_t *vp;
        const pdesc_t *d = ui_page(k, &vp);
        sig = hash_str(sig, d->label) + (uint32_t)(*vp + 32768) * 2654435761u;
        if (ui.view == VIEW_MIXER)
            sig += track[k].mute * 977u;
    }
    sig += (ui.last < 4u && ui.view == VIEW_PAGE ? ui.last + 1u : 0u) * 7919u + ui.view * 31u + ui.kind * 131u +
           ui.dev * 1031u + (ui.kind == FOCUS_SLOT ? tp[sys.sel].engine[ui.slot] * 65537u : 0u);
    if (!ui.force && sig == ui.sig_strip)
        return;
    ui.sig_strip = sig;
    px_colors();
    cv_begin(240, UI_STRIP_H, T_BG);
    for (k = 0; k < 4u; k++) {
        int16_t *vp;
        const pdesc_t *d = ui_page(k, &vp);
        int32_t x = 30 * (int32_t)k, v = *vp;
        uint32_t pk = ui.view == VIEW_MIXER ? PK_FADER
                    : ui.kind == FOCUS_SLOT ? ME_PK[tp[sys.sel].engine[ui.slot]][k] : DEV_PK[ui.dev][k];
        int mute = ui.view == VIEW_MIXER && track[k].mute;
        char val[12];
        const char *unit;
        px_picto(pk, x + 4, 2, d, v, mute ? px_dim : px_ink);
        if (ui.view == VIEW_PAGE && ui.last == k)
            px_tag(x + (30 - px_text_w(PXF_5, d->label)) / 2 - 1, 26, PXF_5, d->label, px_ink, px_bg);
        else
            px_text_c(x, 30, 27, PXF_5, d->label, px_ink);
        if (mute) {
            px_text_c(x, 30, 37, PXF_5, "MUTE", px_ink);
            continue;
        }
        param_format(d, v, val, &unit);
        str_cpy(val + str_len(val), unit, 4);          /* "-140%", "250MS": at most 5, 29 dots */
        px_text_c(x, 30, 37, PXF_5, val, px_dim);
    }
    cv_blit(0, UI_STRIP_Y);
}

/* ------------------------------------------------------------ footer --- */
static void draw_foot(void)
{
    char a[24], b[16];
    uint32_t sig;
    if (ui.glo_held)
        str_cpy(a, "KEYS 1-4: TRACK", sizeof a);
    else if (ui.view == VIEW_MIXER)
        str_cpy(a, "KNOBS: LEVELS", sizeof a);
    else
        str_cpy(a, "KEYS: TEST TONE", sizeof a);
    str_cpy(b, "T", sizeof b);                        /* "T2 OCT 3": the track, its keys' octave */
    fmt_int(b + 1, (int32_t)sys.sel + 1);
    str_cpy(b + str_len(b), " OCT ", 8);
    fmt_int(b + str_len(b), track[sys.sel].octave);
    sig = hash_str(hash_str(5381u, a), b) + track[sys.sel].mute + ux.theme * 3u;
    if (!ui.force && sig == ui.sig_foot)
        return;
    ui.sig_foot = sig;
    px_colors();
    cv_begin(240, UI_FOOT_H, T_BG);
    px_line(0, 1, 119, 1, px_dim, 2);
    px_text(1, 5, PXF_3, a, px_dim);
    {
        int32_t x = 119 - px_text_w(PXF_3, b);
        px_text(x, 5, PXF_3, b, px_ink);
        if (track[sys.sel].mute)                      /* the focused track is muted: say so */
            px_tag(x - 4 - px_text_w(PXF_3, "MUTE"), 4, PXF_3, "MUTE", px_ink, px_bg);
    }
    cv_blit(0, UI_FOOT_Y);
}

/* UPDATE MODE countdown (main.c: OCT- + OCT+ held), over everything below the header. Two canvases: the canvas
 * holds 124 rows (gfx.c CV_MAX), the area is 214 */
static void draw_uboot(void)
{
    char n[4];
    px_colors();
    cv_begin(240, 107, T_BG);
    px_text_c(0, 120, 38, PXF_5B, "UPDATE MODE IN", px_ink);
    cv_blit(0, UI_HEAD_H);
    cv_begin(240, 107, T_BG);
    fmt_int(n, ui.uboot);
    px_text_big(60 - (6 * 4 * (int32_t)str_len(n) - 4) / 2, 0, 4, n, px_ink);
    px_text_c(0, 120, 36, PXF_3, "LET GO OF OCT-/OCT+ TO CANCEL", px_dim);
    cv_blit(0, UI_HEAD_H + 107);
}

static void ui_draw(void)
{
    if (ui.uboot) {
        if (ui.force)
            draw_uboot();
        ui.force = 0;
        return;
    }
    draw_head();
    draw_strip();
    draw_viz();
    draw_foot();
    if (ui.msg_t && !--ui.msg_t)
        ui.sig_viz = 0;                           /* the message ends: the picture comes back */
    if (ui.hot_t)
        ui.hot_t--;
    ui.force = 0;
}

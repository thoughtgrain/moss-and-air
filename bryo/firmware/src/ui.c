/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: the screen. Four regions, each redrawn only when what it shows changes (a signature per region):
 *
 *   y   0..25   header  [TRACK 2 : COLOR], [MOD 1 : WAVE]; the transport and the tempo at the right
 *   y  26..123  strip   the 4-value strip: KNOB 1..4 as dials, their labels and values (PRD 5)
 *   y 124..215  viz     what the page does, drawn from its values (ui_viz.c); the mixer's tracks under GLO
 *   y 216..239  footer  what the keys do now, the track and its octave
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
#define UI_MSG_FRAMES 70             /* a message holds the header ~1 s (~66 frames/s) */

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
    char msg[28];
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
    static const pdesc_t LEVEL[NTRK] = {{"T1 LVL", 0, 127, 100, F_NUM}, {"T2 LVL", 0, 127, 100, F_NUM},
                                        {"T3 LVL", 0, 127, 100, F_NUM}, {"T4 LVL", 0, 127, 100, F_NUM}};
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

static void ui_title(char *b)                /* "[TRACK 2 : COLOR]", "[MOD 1 : WAVE]", "[GLOBAL MIXER]" */
{
    if (ui.view == VIEW_MIXER) {
        str_cpy(b, "[GLOBAL MIXER]", 24);
        return;
    }
    if (ui.kind == FOCUS_SLOT) {
        str_cpy(b, "[MOD ", 24);
        fmt_int(b + str_len(b), (int32_t)ui.slot + 1);
        str_cpy(b + str_len(b), " : ", 4);
        str_cpy(b + str_len(b), ME_NAME[tp[sys.sel].engine[ui.slot]], 8);
    } else {
        str_cpy(b, "[TRACK ", 24);
        fmt_int(b + str_len(b), (int32_t)sys.sel + 1);
        str_cpy(b + str_len(b), " : ", 4);
        str_cpy(b + str_len(b), DEV_NAME[ui.dev], 12);
    }
    str_cpy(b + str_len(b), "]", 2);
}

static uint32_t hash_str(uint32_t h, const char *s)
{
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

/* ------------------------------------------------------------ header --- */
static void draw_head(void)
{
    char ti[28], bpm[8];
    uint32_t sig;
    const char *t = ui.msg_t ? ui.msg : ti;
    ui_title(ti);
    fmt_int(bpm, sys.bpm);
    sig = hash_str(hash_str(2166136261u, t), bpm) + sys.playing * 7u + ui.rec * 131u +
          (ui.kind == FOCUS_SLOT && ui.view == VIEW_PAGE ? 1009u * (ui.slot + 1u) : 0u);
    if (!ui.force && sig == ui.sig_head)
        return;
    ui.sig_head = sig;
    cv_begin(240, UI_HEAD_H, T_BG);
    {   /* right first: REC, the transport (a triangle playing, a square stopped) and the tempo; the title gets the
         * rest (the longest, [TRACK 1 : RESONATOR], is 165 px in M) */
        int32_t x = cv_text_r(234, 3, &AF_M, bpm, T_THEME, T_BG) - 18, xl = 6, right = x;
        if (sys.playing) {
            cv_line_t(x + 2, 7, x + 2, 19, T_ACCENT, 2);
            cv_line_t(x + 2, 7, x + 12, 13, T_ACCENT, 2);
            cv_line_t(x + 2, 19, x + 12, 13, T_ACCENT, 2);
        } else {
            cv_rrect(x + 2, 8, 10, 10, 2, T_MID, T_BG);
        }
        if ((ui.rec >> sys.sel) & 1u) {
            cv_rrect(x - 14, 8, 10, 10, 5, T_REC, T_BG);
            right = x - 14;
        }
        if (!ui.msg_t && ui.kind == FOCUS_SLOT && ui.view == VIEW_PAGE) {   /* the slot's colour, with its number */
            cv_rrect(xl, 5, 6, 15, 2, SLOT_COLOR[ui.slot], T_BG);
            xl += 10;
        }
        cv_text_fit(xl, 3, &AF_M, t, ui.msg_t ? T_ACCENT : T_TEXT, T_BG, right - 6 - xl);
    }
    cv_rect(0, UI_HEAD_H - 1, 240, 1, T_LINE);
    cv_blit(0, 0);
}

/* ------------------------------------------------------------- dials --- */
/* A dial's ring: 270 degrees (7:30 .. 4:30 o'clock), anti-aliased, from the quadrant mask of its size (Felucca's
 * ui_graph.c knob_arc: tools/gen_aa_keycaps.py precomputes coverage and angle for one quadrant; the other three
 * mirror it, no trigonometry at run time). Angles in 1/1024 turn from 12 o'clock, clockwise, -384..384. The arc
 * lo..hi is drawn in vc, the rest of the ring in tr; bg lies under the ring. */
#define KA_END 384
static void dial_arc(int32_t x, int32_t y, int32_t r, const uint8_t *cov, const uint8_t *ang, int32_t lo, int32_t hi,
                     uint16_t tr, uint16_t vc, uint16_t bg)
{
    const uint16_t *rt = ramp(tr, bg), *rv = ramp(vc, bg);
    int32_t i, j, m;
    for (j = 0; j < r; j++)
        for (i = 0; i < r; i++) {
            uint32_t k = (uint32_t)(j * r + i), a = (cov[k >> 1] >> ((k & 1u) ? 0 : 4)) & 15u;
            int32_t q = ang[k];
            if (!a)
                continue;
            for (m = 0; m < 4; m++) {                /* top right, bottom right, bottom left, top left */
                int32_t s = m == 0 ? q : m == 1 ? 512 - q : m == 2 ? q - 512 : -q;
                int32_t px = m < 2 ? x + r + i : x + r - 1 - i, py = (m == 0 || m == 3 ? y + r - 1 - j : y + r + j) + cv_oy;
                if (s < -KA_END || s > KA_END || (uint32_t)px >= cv_w || (uint32_t)py >= cv_h)
                    continue;
                cv_px[(uint32_t)py * cv_w + (uint32_t)px] = (s >= lo && s <= hi ? rv : rt)[a];
            }
        }
}

/* the base position of d's value v: unipolar from the start of the ring, bipolar from 12 o'clock with a nub at 0 */
static void dial(int32_t x, int32_t y, const pdesc_t *d, int32_t v, uint16_t vc, uint16_t bg)
{
    int32_t a0, a1;
    if (d->min < 0) {
        int32_t s = v * KA_END / (v < 0 ? -d->min : d->max);
        a0 = (s < 0 ? s : 0) - 8;
        a1 = (s > 0 ? s : 0) + 8;
    } else {
        a0 = -KA_END;
        a1 = -KA_END + param_ratio(d, v) * 2 * KA_END / 1000;
    }
    dial_arc(x, y, KNOB_BIG_R, KNOB_BIG_COV, KNOB_BIG_ANG, a0, a1, T_DIM, vc, bg);   /* the track stays visible at 0 */
}

/* ------------------------------------------------------------- strip --- */
static void draw_strip(void)
{
    uint32_t k, sig = 2166136261u;
    uint16_t vc = ui.kind == FOCUS_SLOT && ui.view == VIEW_PAGE ? SLOT_COLOR[ui.slot] : T_THEME;
    for (k = 0; k < 4u; k++) {
        int16_t *vp;
        const pdesc_t *d = ui_page(k, &vp);
        sig = hash_str(sig, d->label) + (uint32_t)(*vp + 32768) * 2654435761u;
    }
    sig += vc * 3u + (ui.hot_t ? ui.hot + 1u : 0u) * 7919u;
    if (!ui.force && sig == ui.sig_strip)
        return;
    ui.sig_strip = sig;
    cv_begin(240, UI_STRIP_H, T_BG);
    for (k = 0; k < 4u; k++) {
        int16_t *vp;
        const pdesc_t *d = ui_page(k, &vp);
        int32_t cx = 30 + 60 * (int32_t)k, r = KNOB_BIG_R, v = *vp;
        int hot = ui.hot_t && ui.hot == k;
        char val[12];
        const char *unit;
        int32_t w;
        /* the base arc: the knob's own (unmodulated) position; bipolar ranges grow from 12 o'clock */
        dial(cx - r, 10, d, v, hot ? T_ACCENT : vc, T_BG);
        cv_text_in(cx - 30, 52, 60, &AF_S, d->label, hot ? T_ACCENT : T_MID, T_BG);
        param_format(d, v, val, &unit);
        w = text_w(&AF_M, val) + (unit[0] ? 2 + text_w(&AF_S, unit) : 0);
        {
            int32_t x = cx - w / 2;
            x = cv_text_on(x, 70, &AF_M, val, hot ? T_ACCENT : T_TEXT, T_BG);
            if (unit[0])
                cv_text_on(x + 2, 70, &AF_S, unit, T_MID, T_BG);
        }
    }
    cv_rect(0, UI_STRIP_H - 1, 240, 1, T_LINE);
    cv_blit(0, UI_STRIP_Y);
}

/* ------------------------------------------------------------ footer --- */
static void draw_foot(void)
{
    char a[40], b[16];
    uint32_t sig;
    if (ui.glo_held)
        str_cpy(a, "WHITE KEYS 1-4: TRACK", sizeof a);
    else if (ui.view == VIEW_MIXER)
        str_cpy(a, "KNOBS 1-4: TRACK LEVELS", sizeof a);
    else
        str_cpy(a, "WHITE KEYS: TEST TONE", sizeof a);
    str_cpy(b, "T", sizeof b);                        /* "T2  OCT 3": the track, its keys' octave */
    fmt_int(b + 1, (int32_t)sys.sel + 1);
    str_cpy(b + str_len(b), "  OCT ", 8);
    fmt_int(b + str_len(b), track[sys.sel].octave);
    sig = hash_str(hash_str(5381u, a), b) + track[sys.sel].mute;
    if (!ui.force && sig == ui.sig_foot)
        return;
    ui.sig_foot = sig;
    cv_begin(240, UI_FOOT_H, T_BG);
    cv_rect(0, 0, 240, 1, T_LINE);
    cv_text_fit(6, 6, &AF_S, a, T_MID, T_BG, 160);
    {
        int32_t x = cv_text_r(234, 6, &AF_S, b, T_TEXT, T_BG);
        if (track[sys.sel].mute)
            cv_text_r(x - 6, 6, &AF_S, "MUTE", T_ACCENT, T_BG);   /* the focused track is muted: say so */
    }
    cv_blit(0, UI_FOOT_Y);
}

/* UPDATE MODE countdown (main.c: OCT- + OCT+ held), over everything below the header. Two canvases: the canvas
 * holds 124 rows (gfx.c CV_MAX), the area is 214 */
static void draw_uboot(void)
{
    char n[4];
    cv_begin(240, 107, T_BG);
    cv_text_in(0, 70, 240, &AF_M, "UPDATE MODE IN", T_TEXT, T_BG);
    cv_blit(0, UI_HEAD_H);
    cv_begin(240, 107, T_BG);
    fmt_int(n, ui.uboot);
    cv_text_in(0, 0, 240, &AF_L, n, T_THEME, T_BG);
    cv_text_in(0, 46, 240, &AF_S, "LET GO OF OCT- AND OCT+ TO CANCEL", T_MID, T_BG);
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
        ui.sig_head = 0;                          /* the message ends: the title comes back */
    if (ui.hot_t)
        ui.hot_t--;
    ui.force = 0;
}

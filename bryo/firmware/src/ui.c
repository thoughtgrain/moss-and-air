/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: the screen. Four regions, each redrawn only when what it shows changes (a signature per region), all on
 * the dot grid of ui_px.c (2 x 2 px dots, one ink; dot rows in brackets):
 *
 *   y   0..25   header  [T1] COLOR, [M1] WAVE in bold, the inverted tempo bar with REC and the transport  (0..12)
 *   y  26..123  strip   KNOB 1..4 as pictograms that show their values, the labels and the values (PRD 5)  (13..61)
 *   y 124..215  viz     what the page does, drawn from its values (ui_viz.c); the mixer: the track's channel  (62..107)
 *   y 216..239  footer  what the keys do now, the track and its octave                                      (108..119)
 *
 * The tracks themselves live in the mixer (GLO): hold GLO and press white key 1..4 to pick one. The pages don't
 * repeat them; the header and the footer name the focused track.
 *
 * Main loop only. The audio ISR never touches the UI; the UI reads the ISR's meters (track_rt[].peak) as plain
 * words. One ink for now: emphasis is inversion and borders, never colour. */

#define UI_HEAD_H 26
#define UI_STRIP_Y 26
#define UI_STRIP_H 98
#define UI_VIZ_Y 124
#define UI_FOOT_Y 216
#define UI_FOOT_H 24
#define UI_MSG_FRAMES 70             /* a message holds the visualization panel ~1 s (~66 frames/s) */

enum { FOCUS_DEV, FOCUS_SLOT };      /* what the strip shows: a device of the track, or a modulator slot */
enum { VIEW_PAGE, VIEW_MIXER, VIEW_ROUTE, VIEW_USBREC };   /* VIEW_MIXER: GLO, the four track levels on the knobs,
                                                            * the tracks below; VIEW_ROUTE: ALGORITHM, each track's
                                                            * REC IN on the knobs; VIEW_USBREC: the USB record mode
                                                            * (REC held), the tracks a take can go to */

static struct {
    uint8_t kind;                    /* FOCUS_DEV / FOCUS_SLOT */
    uint8_t dev;                     /* DEV_* when FOCUS_DEV */
    uint8_t slot;                    /* 0..3 when FOCUS_SLOT */
    uint8_t view;
    uint8_t page;                    /* the focused device's or slot's page, 0 or 1 (its pad pressed again) */
    uint8_t hot, hot_t;              /* the knob just turned (its dial and value in the accent), frames left */
    uint8_t last;                    /* the page's most recently turned knob (0..3; 0xFF none since the page opened):
                                      * the visualization draws its part in the accent and names it */
    uint8_t uboot;                   /* UPDATE MODE countdown, seconds left (main.c), 0 = none */
    uint8_t glo_held;                /* GLO held: the mixer is up, white keys 1..4 pick the track */
    uint8_t glo_used;                /* .. and something was done while it was held (so letting go closes it) */
    uint8_t glo_latched;             /* the mixer stays up (GLO tapped) */
    uint8_t poly_held, save_held;    /* the POLY key (clear the tape) and SAVE (undo) held, waiting for HOLD */
    uint32_t poly_t0, save_t0;
    uint8_t zero_held;               /* the 0 key down: let go before HOLD, a tap (the freeze latches or lets go) */
    uint8_t rec_held, rec_said;      /* REC down (stopped: its arm waits for the let-go; held a second: USB record),
                                      * and the hint shown */
    uint32_t rec_t0;
    uint32_t zero_t0;
    uint8_t chan;                    /* the mixer's channel page: the knobs set the selected track's channel strip */
    uint8_t chan_held, chan_used;    /* EDIT down on the mixer, and a knob turned meanwhile (as GLO's: held momentary,
                                      * tapped latched) */
    uint8_t chan_latched;
    uint32_t chan_t0;
    uint32_t glo_t0;                 /* when GLO went down */
    uint8_t force;                   /* redraw everything next frame */
    uint8_t msg_t;
    char msg[40];
    uint32_t sig_head, sig_strip, sig_viz, sig_foot;
} ui;

static void draw_viz(void);           /* ui_viz.c */
static void viz_usbrec_strip(void);   /* ui_viz.c: the record mode's tracks */

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
    if (ui.view == VIEW_MIXER && ui.chan) {
        *vp = &p->ch[k];
        return &CH_P[k];
    }
    if (ui.view == VIEW_MIXER) {
        lv[k] = track[k].level;
        *vp = &lv[k];
        return &LEVEL[k];
    }
    if (ui.view == VIEW_ROUTE) {
        lv[k] = tp[k].recin;
        *vp = &lv[k];
        return &RIN_P[k];
    }
    k += 4u * ui.page;
    if (ui.kind == FOCUS_SLOT) {
        *vp = &p->mod[ui.slot][k];
        return &ME_P[p->engine[ui.slot]][k];
    }
    *vp = &dev_v(sys.sel, ui.dev)[k];                  /* (the source: the chosen source's knobs) */
    return &dev_p(sys.sel, ui.dev)[k];
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
    uint32_t pages = ui.view != VIEW_PAGE ? 1u
                   : pdesc_pages(slot ? ME_P[tp[sys.sel].engine[ui.slot]] : dev_p(sys.sel, ui.dev));
    if (ui.view == VIEW_USBREC) {
        str_cpy(ti, "USB RECORD", sizeof ti);
        str_cpy(box, "IN", sizeof box);
    } else if (ui.view == VIEW_MIXER)
        str_cpy(ti, ui.chan ? "CHANNEL" : "MIXER", sizeof ti);
    else if (ui.view == VIEW_ROUTE)
        str_cpy(ti, "REC IN", sizeof ti);
    else
        str_cpy(ti, slot ? ME_NAME[tp[sys.sel].engine[ui.slot]] : dev_name(sys.sel, ui.dev), sizeof ti);
    if (slot) {
        box[0] = 'M';
        box[1] = (char)('1' + ui.slot);
    }
    fmt_int(bpm, sys.bpm);
    sig = hash_str(hash_str(hash_str(2166136261u, ti), bpm), box) + pages * 977u + ui.page * 61u + sys.playing * 7u + ((sys.rec >> sys.sel) & 1u) * 131u +
          ux.theme * 3u + (ur.state == UR_RECORDING) * 524287u;
    if (!ui.force && sig == ui.sig_head)
        return;
    ui.sig_head = sig;
    px_colors();
    px_begin(UI_HEAD_H);
    {
        int32_t bw = px_text_w(PXF_5, box) + 4, x = bw + 2, bar;
        px_frame(0, 1, bw, 11, px_ink, 1);
        px_text(2, 3, PXF_5, box, px_ink);
        x = px_text(x, 3, PXF_5B, ti, px_ink);
        if (pages > 1u) {                                  /* the pages, side by side: the shown one a block, the other a dot */
            uint32_t g;
            for (g = 0; g < pages; g++) {
                if (g == ui.page)
                    px_box(x + 2 + (int32_t)g * 4, 5, 3, 3, px_ink);
                else
                    px_dot(x + 3 + (int32_t)g * 4, 6, px_ink);
            }
            x += 2 + 4 * (int32_t)pages;
        }
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
        if (ui.view == VIEW_USBREC ? ur.state == UR_RECORDING : (sys.rec >> sys.sel) & 1u) {   /* REC armed (the record
                                                                                             * mode: recording): a dot */
            x -= 7;
            px_box(x + 1, 4, 3, 5, px_bg);
            px_box(x, 5, 5, 3, px_bg);
        }
    }
    px_blit(0);
}

/* ------------------------------------------------------------- strip --- */
/* four cells of 30 dots: the knob's pictogram (it shows the value), its label, the value. The page's last-turned
 * knob has its label inverted. On the mixer each cell is a track: a fader whose notches are the level set and whose
 * fill is the live meter (so the strip redraws as the meters move); EDIT (held, or tapped to latch): the selected track's channel. */
static void draw_strip(void)
{
    uint32_t k, sig = 2166136261u + ux.theme * 3u;
    if (ui.view == VIEW_USBREC) {                       /* the record mode: the tracks a take can go to */
        sig += ur.state * 7u + (uint32_t)(ur.dest + 1) * 131u + sys.ntrk * 1031u + sys.sel * 7919u + ui.last * 3u;
        for (k = 0; k < 4u; k++)
            sig = (sig ^ (uint32_t)(ur.kv[k] + 32768)) * 16777619u;
        for (k = 0; k < NTRK; k++)
            sig = (sig ^ (tape_ctl[k].nblk + tape_ctl[k].empty * 65536u)) * 16777619u;
        if (!ui.force && sig == ui.sig_strip)
            return;
        ui.sig_strip = sig;
        px_colors();
        px_begin(UI_STRIP_H);
        viz_usbrec_strip();
        px_blit(UI_STRIP_Y);
        return;
    }
    for (k = 0; k < 4u; k++) {
        int16_t *vp;
        const pdesc_t *d = ui_page(k, &vp);
        sig = hash_str(sig, pdesc_empty(d) ? "" : d->label) + (uint32_t)(*vp + 32768) * 2654435761u;
        if (ui.view == VIEW_MIXER && !ui.chan)          /* the faders carry the meters */
            sig = (sig ^ (uint32_t)(meter_w(track_rt[k].peak, 19) | track[k].mute << 8)) * 16777619u;
    }
    sig += (ui.last < 4u ? ui.last + 1u : 0u) * 7919u + sys.ntrk * 15485863u + sys.sel * 104729u + ui.view * 31u + ui.page * 263u + ui.chan * 5u + ui.kind * 131u +
           ui.dev * 1031u + (ui.kind == FOCUS_SLOT ? tp[sys.sel].engine[ui.slot] * 65537u : tp[sys.sel].src * 3571u);
    if (!ui.force && sig == ui.sig_strip)
        return;
    ui.sig_strip = sig;
    px_colors();
    px_begin(UI_STRIP_H);
    for (k = 0; k < 4u; k++) {
        int16_t *vp;
        const pdesc_t *d = ui_page(k, &vp);
        int32_t x = 30 * (int32_t)k, v = *vp;
        if (pdesc_empty(d)) {                          /* an unused knob on this page: an empty cell */
            px_frame(x + 9, 8, 12, 12, px_dim, 2);
            continue;
        }
        uint32_t pk = ui.view == VIEW_ROUTE ? PK_SRC : ui.view == VIEW_MIXER ? CH_PK[k]
                    : ui.kind == FOCUS_SLOT ? ME_PK[tp[sys.sel].engine[ui.slot]][4u * ui.page + k]
                    : dev_pk(sys.sel, ui.dev)[4u * ui.page + k];
        int lvl = ui.view == VIEW_MIXER && !ui.chan, mute = lvl && track[k].mute;
        int off = (lvl || ui.view == VIEW_ROUTE) && k >= sys.ntrk;
        char val[12];
        const char *unit;
        if ((lvl || ui.view == VIEW_ROUTE) && k == sys.sel)   /* the selected track (its channel is the one below) */
            px_frame(x, 0, 30, 48, px_ink, 1);
        if (lvl)                                       /* the level set, and the live meter inside it */
            px_meter_fader(x + 4, 2, param_ratio(d, v), meter_w(track_rt[k].peak, 19), mute || off ? px_dim : px_ink);
        else
            px_picto(pk, x + 4, 2, d, v, off ? px_dim : px_ink);
        if (ui.last == k)
            px_tag(x + (30 - px_text_w(PXF_5, d->label)) / 2 - 1, 26, PXF_5, d->label, px_ink, px_bg);
        else
            px_text_c(x, 30, 27, PXF_5, d->label, px_ink);
        if (off || mute) {                             /* parked (TRACKS below it), or muted */
            px_text_c(x, 30, 37, PXF_5, off ? "OFF" : "MUTE", off ? px_dim : px_ink);
            continue;
        }
        param_format(d, v, val, &unit);
        str_cpy(val + str_len(val), unit, 4);          /* "-140%", "250MS": at most 5, 29 dots */
        px_text_c(x, 30, 37, PXF_5, val, px_ink);
    }
    px_blit(UI_STRIP_Y);
}

/* ------------------------------------------------------------ footer --- */
static void draw_foot(void)
{
    char a[28], b[16];
    uint32_t sig;
    if (ui.view == VIEW_USBREC)                       /* the record mode: what REC and HOME do in this step */
        str_cpy(a, ur.state == UR_RECORDING || ur.state == UR_CHOOSE ? (ur.state == UR_CHOOSE ? "REC:KEEP HOME:THROW AWAY"
                   : "REC:STOP HOME:THROW AWAY") : "REC:START HOME:LEAVE", sizeof a);
    else if (ui.glo_held)                             /* the white keys pick the track; SELECT sets TRACKS */
        str_cpy(a, "KEYS:TRK SEL:TRACKS", sizeof a);
    else if (ui.view == VIEW_MIXER)
        str_cpy(a, ui.chan ? "KNOBS: CHANNEL" : "EDIT: CHANNEL", sizeof a);
    else if (ui.view == VIEW_ROUTE)
        str_cpy(a, "KNOBS: REC IN", sizeof a);
    else
        str_cpy(a, tp[sys.sel].src != SRC_TAPE ? "KEYS: NOTES" : ui.kind == FOCUS_DEV && ui.dev == DEV_GRAIN ?
                   "KEYS: GRAIN CURSOR" : ui.kind == FOCUS_DEV && ui.dev == DEV_RESO ? "KEYS: PLUCK STRINGS" :
                   "KEYS: SLICES", sizeof a);   /* (what the keys do on this page) */
    str_cpy(b, "T", sizeof b);                        /* "T2 OCT 3": the track, its keys' octave */
    fmt_int(b + 1, (int32_t)sys.sel + 1);
    str_cpy(b + str_len(b), " OCT ", 8);
    fmt_int(b + str_len(b), track[sys.sel].octave);
    if (ui.view == VIEW_USBREC)
        str_cpy(b, "44.1K", sizeof b);                /* (what the computer sends: 44.1 kHz) */
    sig = hash_str(hash_str(5381u, a), b) + track[sys.sel].mute + ux.theme * 3u;
    if (!ui.force && sig == ui.sig_foot)
        return;
    ui.sig_foot = sig;
    px_colors();
    px_begin(UI_FOOT_H);
    px_line(0, 1, 119, 1, px_dim, 2);
    px_text(1, 5, PXF_3, a, px_dim);
    {
        int32_t x = 119 - px_text_w(PXF_3, b);
        px_text(x, 5, PXF_3, b, px_ink);
        if (track[sys.sel].mute && ui.view != VIEW_USBREC)   /* the focused track is muted: say so */
            px_tag(x - 4 - px_text_w(PXF_3, "MUTE"), 4, PXF_3, "MUTE", px_ink, px_bg);
    }
    px_blit(UI_FOOT_Y);
}

/* UPDATE MODE countdown (main.c: OCT- + OCT+ held), over everything below the header: two strips (106 + 108 rows,
 * both on the dot grid; the canvas holds 54 dot rows) */
static void draw_uboot(void)
{
    char n[4];
    px_colors();
    px_begin(106);
    px_text_c(0, 120, 38, PXF_5B, "UPDATE MODE IN", px_ink);
    px_blit(UI_HEAD_H);
    px_begin(108);
    fmt_int(n, ui.uboot);
    px_text_big(60 - (6 * 4 * (int32_t)str_len(n) - 4) / 2, 0, 4, n, px_ink);
    px_text_c(0, 120, 36, PXF_3, "LET GO OF OCT-/OCT+ TO CANCEL", px_dim);
    px_blit(UI_HEAD_H + 106);
}

/* HARDWARE CALIBRATION (panel.c panel_setup, OCT- + OCT+ held at power-on), on the dot grid like everything else:
 * the title and what to do; then, per control, the instruction and the control's name, large */
static void ui_setup_title(void)
{
    px_colors();
    lcd_fill(0, 0, 240, 240, px_bg);
    px_begin(60);
    px_text_c(0, 120, 3, PXF_3, "HARDWARE", px_dim);   /* (the whole title is wider than the bold face fits) */
    px_text_c(0, 120, 10, PXF_5B, "CALIBRATION", px_ink);
    px_text_c(0, 120, 20, PXF_3, "TEACH EACH BUTTON AND KNOB", px_dim);
    px_line(8, 28, 111, 28, px_dim, 2);
    px_blit(0);
}

static void ui_setup_show(const char *what, const char *name)   /* "PRESS" / "TURN RIGHT", the control */
{
    px_colors();
    px_begin(60);
    px_text_c(0, 120, 4, PXF_5, what, px_dim);
    px_text_big(60 - (12 * (int32_t)str_len(name) - 2) / 2, 16, 2, name, px_ink);
    px_blit(80);
}

/* Felucca's one-line text (gfx.c draw_text_line), on the dot grid (bryo.c GFX_DOT_TEXT): the boot screen, UBOOT,
 * the crash screen and the update's progress (ota_hw.c) keep their calls and their rows, in the dot font sized to
 * the line (AF_S: 3 x 5, AF_M: 5 x 7 bold, AF_L: 5 x 7 at 2x). x, y, w are pixels; the strip is the line's rows,
 * full width. */
static void draw_text_line(uint32_t x, uint32_t y, uint32_t w, const aafont_t *f, const char *s, uint16_t c,
                           uint16_t bg, int align)
{
    uint32_t h = ((uint32_t)f->h + 1u) & ~1u, font = f == &AF_S ? PXF_3 : PXF_5B;
    int32_t big = f == &AF_L, th = big ? 14 : font == PXF_3 ? 5 : 7;
    int32_t tw = big ? 12 * (int32_t)str_len(s) - 2 : px_text_w(font, s), dx = (int32_t)x / 2, dw = (int32_t)w / 2;
    int32_t tx = align == 1 ? dx + (dw - tw) / 2 : align == 2 ? dx + dw - tw : dx, ty = ((int32_t)h / 2 - th) / 2;
    px_colors();
    px_begin(h);
    pxc_pal[0] = bg;                                    /* (the line's own colours) */
    pxc_pal[2] = c;
    if (big)
        px_text_big(tx, ty, 2, s, c);
    else
        px_text(tx, ty, font, s, c);
    px_blit(y & ~1u);
    lcd_sync();                                         /* one-shots (boot, crash, UBOOT, update) finish here */
}
static void draw_text_box(uint32_t x, uint32_t y, uint32_t w, const aafont_t *f, const char *s, uint16_t c, int align)
{
    draw_text_line(x, y, w, f, s, c, T_BG, align);
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

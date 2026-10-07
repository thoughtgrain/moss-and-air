/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo on the host: the app's own sources (the chain, the parameter table, the UI and its input) on stubs of the
 * display and the input scan.
 *
 *   bryo_host OUT_DIR        writes OUT_DIR/ppm/<palette>_<screen>.ppm and prints one line per check
 *
 * Audio: silence at rest, a held key's pitch, mute, a click-free release, load shedding, the master stage's
 *        ceiling. Input: what each pad, key and knob does (PRD 2, phase 1). Screens: every phase 1 screen in
 *        every palette, for tests/ui_golden.py (pixel fingerprints) and the eye (tests/bryo_ui.sh makes PNGs). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define __attribute__(x)
#define memset felucca_memset            /* the firmware's own (libc.c), not the host's */
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#define FELUCCA_FLASH 0
#define FELUCCA_CDC 0
#define FELUCCA_VERSION "TEST"
#include "felucca_tables.h"
#include "../firmware/src/libc.c"

/* ------------------------------------------------------------ HAL stubs --- */
#define FM1_NCOL 11u
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}};
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL];
static void fm1_led_dim_level(uint32_t lo) { (void)lo; }
#define FM1_TICKS_PER_US 1u
static struct { uint32_t notes, buttons; } fm1_in;
static uint32_t host_pressed, host_note_edges;
static int32_t host_enc[7];
static uint32_t fm1_ticks(void) { return 0; }
static uint32_t fm1_input_edges(uint32_t *r) { uint32_t p = host_pressed; if (r) *r = 0; host_pressed = 0; return p; }
static uint32_t fm1_input_note_edges(void) { uint32_t n = host_note_edges; host_note_edges = 0; return n; }
static int32_t fm1_enc_take(uint32_t e) { int32_t s = host_enc[e % 7u]; host_enc[e % 7u] = 0; return s; }
static void fm1_wdt_feed(void) {}
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
static uint16_t host_screen[240 * 240];
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            host_screen[(y + j) * 240u + x + i] = (uint16_t)((c >> 8) | (c << 8));
}
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            host_screen[(y + j) * 240u + x + i] = p[j * w + i];
}

/* ------------------------------------------------------- Bryo sources --- */
#include "../firmware/src/gfx.c"
#include "../firmware/src/bryo.h"
static void ui_message(const char *s);
static void ui_redraw(void);
#include "../firmware/src/dsp.c"
#include "../firmware/src/master.c"
#include "../firmware/src/param.c"
#include "../firmware/src/chain.c"
#include "../firmware/src/icons.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/settings.c"
#include "../firmware/src/ui_px.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/ui_viz.c"
#include "../firmware/src/ui_input.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("bryo: %-78s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static void power_on(void)
{
    memset(&ui, 0, sizeof ui);
    memset(track, 0, sizeof track);
    memset(track_rt, 0, sizeof track_rt);
    memset(&sys, 0, sizeof sys);
    memset(&fm1_in, 0, sizeof fm1_in);
    panel = PANEL_DEFAULT;
    param_defaults();
    chain_init();
    ui_init();
    sys.bpm = 120;
    sys.master_q12 = 2048;
    sys.keys_live = 1;
}

/* ---------------------------------------------------------------- audio --- */
static int32_t out[2 * CTL];
static void render(uint32_t blocks, int32_t *buf)        /* buf: blocks * CTL left samples, or 0 */
{
    uint32_t b, i;
    for (b = 0; b < blocks; b++) {
        chain_block(out, CTL);
        if (buf)
            for (i = 0; i < CTL; i++)
                buf[b * CTL + i] = out[2u * i];
    }
}

static uint32_t note_bit_of_white(uint32_t w)
{
    uint32_t n;
    for (n = 0; n < 27u; n++)
        if (KEY_WHITE[n] == w)
            return 1u << n;
    return 0;
}

static void test_audio(void)
{
    enum { NB = 1378 };                                  /* 1 s */
    static int32_t s[NB * CTL];
    uint32_t i, cross = 0;
    int32_t peak = 0, step = 0;
    power_on();
    render(NB, s);
    for (i = 0; i < NB * CTL; i++)
        peak = abs(s[i]) > peak ? abs(s[i]) : peak;
    check("silence at rest: every sample 0", peak == 0);

    fm1_in.notes = note_bit_of_white(0);                 /* white key 1 = F, octave 3: MIDI 53, 174.61 Hz */
    render(NB, s);
    for (i = CTL * 100u; i < NB * CTL; i++)              /* (after the attack) */
        cross += s[i - 1] < 0 && s[i] >= 0;
    peak = 0;
    for (i = 0; i < NB * CTL; i++)
        peak = abs(s[i]) > peak ? abs(s[i]) : peak;
    {
        double hz = cross * (double)FS / (double)((NB - 100u) * CTL);
        char b[96];
        snprintf(b, sizeof b, "a held white key 1 plays F3: %.1f Hz (174.6 expected, +-1 Hz)", hz);
        check(b, fabs(hz - 174.61) < 1.0);
        snprintf(b, sizeof b, "its level sits under the limiter: peak %d (> 2000, < 18000)", (int)peak);
        check(b, peak > 2000 && peak < 18000);
    }

    fm1_in.notes = 0;                                    /* release: a fade, no step */
    render(NB / 4, s);
    for (i = 1; i < NB / 4 * CTL; i++) {
        int32_t d = abs(s[i] - s[i - 1]);
        step = d > step ? d : step;
    }
    peak = 0;
    for (i = NB / 4 * CTL - CTL; i < NB / 4 * CTL; i++)
        peak = abs(s[i]) > peak ? abs(s[i]) : peak;
    check("the release fades: no sample-to-sample step above a sine's own slope (< 800)", step < 800);
    check("..and ends in silence within 250 ms", peak == 0);
    check("..and frees its voice", track_rt[0].key[0] == 0xFFu);

    track[0].mute = 1;                                   /* mute: silence while held */
    fm1_in.notes = note_bit_of_white(0);
    render(200, s);
    peak = 0;
    for (i = 0; i < 200u * CTL; i++)
        peak = abs(s[i]) > peak ? abs(s[i]) : peak;
    check("a muted track is silent while its key is held", peak == 0);
    track[0].mute = 0;

    sys.keys_live = 0;                                   /* SEL held: the keys pick, they don't play */
    fm1_in.notes = note_bit_of_white(4);
    render(4, 0);
    check("keys don't play while SEL is held (keys_live 0)", track_rt[0].key[1] == 0xFFu);
    sys.keys_live = 1;

    fm1_in.notes = note_bit_of_white(0) | note_bit_of_white(2) | note_bit_of_white(4);
    render(8, 0);
    {
        uint32_t v, n = 0, before = chain_shed_count;
        chain_shed();
        for (v = 0; v < TEST_VOICES; v++)
            n += track_rt[0].key[v] != 0xFFu;
        check("shedding frees one voice (3 held -> 2 sounding) and counts it", n == 2u && chain_shed_count == before + 1u);
    }
    fm1_in.notes = 0;
    render(NB / 2, 0);

    {   /* the master stage holds the ceiling: 4 tracks x 4 keys at full level */
        uint32_t t;
        int32_t over = 0;
        for (t = 0; t < NTRK; t++)
            track[t].level = 127;
        sys.master_q12 = MASTER_FULL;
        fm1_in.notes = note_bit_of_white(0) | note_bit_of_white(2) | note_bit_of_white(4) | note_bit_of_white(6);
        for (t = 0; t < NTRK; t++) {
            sys.sel = (uint8_t)t;
            render(40, s);
        }
        render(NB, s);
        for (i = 0; i < NB * CTL; i++)
            over = abs(s[i]) > over ? abs(s[i]) : over;
        check("four keys at full level and MASTER full stay under full scale (soft clip)", over <= 32767);
    }
}

/* ---------------------------------------------------------------- input --- */
static void press(uint32_t b) { host_pressed |= 1u << panel.btn[b]; ui_input(); }
static void hold(uint32_t b) { fm1_in.buttons |= 1u << panel.btn[b]; host_pressed |= 1u << panel.btn[b]; ui_input(); }
static void let_go(uint32_t b) { fm1_in.buttons &= ~(1u << panel.btn[b]); ui_input(); }
static void tap(uint32_t b) { hold(b); let_go(b); }
static void key_edge(uint32_t note) { host_note_edges |= 1u << note; ui_input(); }
static uint32_t black_note(uint32_t k)
{
    uint32_t n;
    for (n = 0; n < 27u; n++)
        if (KEY_BLACK[n] == k)
            return n;
    return 0;
}

static void test_input(void)
{
    power_on();
    check("power-on: TRACK 1, the source (TAPE) focused", sys.sel == 0 && ui.kind == FOCUS_DEV && ui.dev == DEV_SRC);
    press(B_EDIT);
    check("EDIT: GRAIN", ui.kind == FOCUS_DEV && ui.dev == DEV_GRAIN);
    press(B_EDIT);
    check("EDIT again: RESONATOR", ui.dev == DEV_RESO);
    press(B_EDIT);
    check("EDIT again: GRAIN", ui.dev == DEV_GRAIN);
    press(B_FX);
    check("FX: COLOR", ui.dev == DEV_COLOR);
    press(B_FX);
    check("FX again: SPACE", ui.dev == DEV_SPACE);
    press(B_ENV);
    check("ENV: modulator slot 2", ui.kind == FOCUS_SLOT && ui.slot == 1u);
    press(B_HOME);
    check("HOME: the source again", ui.kind == FOCUS_DEV && ui.dev == DEV_SRC);

    host_enc[panel.enc[EN_K2]] = 3;                      /* KNOB 2 on TAPE: LENGTH 100 -> clamps at 100 */
    host_enc[panel.enc[EN_K3]] = -10;                    /* KNOB 3: SPEED 100 % -> 100 - 10 * 4 = 60 % */
    ui_input();
    check("KNOB 2 clamps at the range's top (LENGTH 100)", tp[0].dev[DEV_SRC][1] == 100);
    check("KNOB 3 steps a wide range by ~1 % per detent (SPEED 100 -> 60)", tp[0].dev[DEV_SRC][2] == 60);
    check("..and lights that dial (hot)", ui.hot == 2u && ui.hot_t > 0u);

    check("a turned knob becomes the page's last (the visualization names it)", ui.last == 2u);
    press(B_EDIT);
    check("..and a new page starts without one", ui.last == 0xFFu);
    press(B_HOME);

    hold(B_GLO);                                         /* GLO held + white key 3: TRACK 3 */
    check("GLO held: the mixer is up, the keys stop playing", ui.view == VIEW_MIXER && sys.keys_live == 0u);
    key_edge(2u + 2u);                                   /* note 4 = A3 = white key 3 */
    check("GLO + white key 3: TRACK 3", sys.sel == 2u);
    let_go(B_GLO);
    check("GLO let go after picking: back to the page, the keys play again", ui.view == VIEW_PAGE && sys.keys_live == 1u);
    check("each track keeps its own values (TRACK 3's SPEED at the default)", tp[2].dev[DEV_SRC][2] == 100);
    press(B_SCL);
    check("SCL does nothing now (the PRD's SEL moved under GLO)", sys.sel == 2u && ui.view == VIEW_PAGE);

    key_edge(black_note(BK_OP2));
    check("black OP2: TRACK 2 muted", track[1].mute == 1u);
    key_edge(black_note(BK_OP2));
    check("..again: unmuted", track[1].mute == 0u);

    tap(B_GLO);
    check("GLO tapped: the mixer stays up", ui.view == VIEW_MIXER && ui.glo_latched);
    host_enc[panel.enc[EN_K4]] = -20;
    ui_input();
    check("..KNOB 4 sets TRACK 4's level (100 -> 80)", track[3].level == 80u);
    tap(B_GLO);
    check("GLO tapped again: back to the page", ui.view == VIEW_PAGE);
    hold(B_GLO);
    host_enc[panel.enc[EN_K1]] = 3;
    ui_input();
    let_go(B_GLO);
    check("GLO held + KNOB 1: TRACK 1's level, and letting go goes back", track[0].level == 103u && ui.view == VIEW_PAGE);
    tap(B_GLO);
    hold(B_EDIT);
    check("EDIT held on the mixer: the channel page, the mixer stays", ui.chan && ui.view == VIEW_MIXER);
    host_enc[panel.enc[EN_K3]] = -30;
    ui_input();
    check("..KNOB 3 sets the selected track's FILT (TRACK 3: 0 -> -30), not a level", tp[2].ch[CH_FILT] == -30 &&
          track[2].level == 100u);
    let_go(B_EDIT);
    check("EDIT let go: the levels again, the mixer still up", !ui.chan && ui.view == VIEW_MIXER);
    tp[2].ch[CH_FILT] = 0;
    press(B_FX);
    check("a page pad closes the mixer", ui.view == VIEW_PAGE && ui.dev == DEV_COLOR && !ui.glo_latched);
    press(B_HOME);

    host_enc[panel.enc[EN_SELECT]] = 5;
    ui_input();
    check("SELECT: the tempo (120 -> 125)", sys.bpm == 125u);
    press(B_PLAY);
    check("PLAY: playing", sys.playing == 1u);
    press(B_REC);
    check("REC: TRACK 3 armed", ui.rec == 1u << 2);
    press(B_OCTUP);
    check("OCT+: TRACK 3's keys an octave up", track[2].octave == 4u);
}

/* ------------------------------------------------------------- settings --- */
/* a record Felucca 1.0.3 saved (PER4): Bryo keeps its calibration, palette and USB prefs, and saves Felucca's own
 * fields back untouched */
static void test_settings(void)
{
    persist_t p, q;
    uint32_t i;
    int ok = 1;
    memset(&p, 0, sizeof p);
    p.magic = PERSIST_MAGIC;
    p.palette = palette_to_stored(3);
    p.lowcut = 2;
    p.panel = PANEL_DEFAULT;
    p.panel.btn[B_PLAY] = PANEL_DEFAULT.btn[B_REC];      /* a learned table: PLAY and REC swapped */
    p.panel.btn[B_REC] = PANEL_DEFAULT.btn[B_PLAY];
    for (i = 0; i < sizeof p.felucca; i++)
        p.felucca[i] = (uint8_t)(i * 7u + 1u);           /* Felucca's favorites and MENU flags: any bytes */
    p.felucca[PREFS_AT] = PREF_SERIAL_OFF | PREF_USB_FIXED;
    panel = PANEL_DEFAULT;
    check("a Felucca PER4 record is read as current", settings_import(&p, (int)sizeof p) == 1);
    check("..its learned panel table is Bryo's", panel.btn[B_PLAY] == PANEL_DEFAULT.btn[B_REC]);
    check("..USB SERIAL OFF and USB LEVEL FIXED hold", (ui_prefs & PREF_SERIAL_OFF) && fx_usb_fixed == 1u);
    check("..the palette and SPEAKER EQ hold", settings.palette == 3u && settings.lowcut == 2u);
    q = p;
    settings_export(&q);
    for (i = 0; i < sizeof q.felucca; i++)
        ok &= q.felucca[i] == p.felucca[i];
    check("a Bryo save writes Felucca's fields back byte for byte", ok && !memcmp(&q.panel, &p.panel, sizeof q.panel));
    check("a record of the wrong size is refused (no partial read)", settings_import(&p, (int)sizeof p - 1) == 0);
    panel = PANEL_DEFAULT;
    ui_prefs = 0;
    fx_usb_fixed = 0;
}

/* -------------------------------------------------------------- screens --- */
static const char *out_dir;
static void shot(const char *pal, const char *name)
{
    char path[512];
    FILE *f;
    uint32_t i;
    ui.force = 1;
    ui_draw();
    snprintf(path, sizeof path, "%s/ppm/%s_%s.ppm", out_dir, pal, name);
    f = fopen(path, "wb");
    if (!f) {
        check(path, 0);
        return;
    }
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = host_screen[i];
        c = (uint16_t)((c >> 8) | (c << 8));             /* (the blit holds the LCD's byte order) */
        fputc(((c >> 11) & 31) * 255 / 31, f);
        fputc(((c >> 5) & 63) * 255 / 63, f);
        fputc((c & 31) * 255 / 31, f);
    }
    fclose(f);
}

static void turn(uint32_t k, int32_t d) { host_enc[panel.enc[EN_K1 + k]] = d; ui_input(); }

static void screens_in(const char *pal)
{
    uint32_t s;
    power_on();
    lcd_fill(0, 0, 240, 240, T_BG);
    shot(pal, "tape");
    turn(2, -60);                                        /* SPEED to -140 %: reversed, the chevrons in the accent */
    shot(pal, "tape_speed");
    turn(0, 20);                                         /* START 20 %, LENGTH 60 %, DUB 80 % */
    turn(1, -40);
    turn(3, 30);
    shot(pal, "tape_loop");
    press(B_EDIT);
    shot(pal, "grain");
    turn(1, 40);                                         /* DENS 80 %, PITCH +7, SPREAD 90 % */
    turn(2, 7);
    turn(3, 60);
    shot(pal, "grain_busy");
    press(B_EDIT);
    shot(pal, "resonator");
    turn(1, 35);                                         /* FDBK 95: sharp peaks */
    turn(2, -30);                                        /* DAMP 10 */
    turn(3, 80);                                         /* MIX 80 */
    shot(pal, "resonator_wet");
    press(B_FX);
    shot(pal, "color");
    turn(0, 60);                                         /* DRIVE 60: the transfer curve */
    shot(pal, "color_drive");
    turn(1, 70);                                         /* CRUSH 70: steps */
    shot(pal, "color_crush");
    turn(2, 50);                                         /* NOISE 50 */
    shot(pal, "color_noise");
    turn(3, -30);                                        /* TONE 20: the tone filter */
    shot(pal, "color_tone");
    press(B_FX);
    shot(pal, "space");
    turn(1, 40);                                         /* FDBK 70 */
    turn(3, 40);                                         /* DECAY 80 */
    shot(pal, "space_long");
    for (s = 0; s < NSLOT; s++) {
        static const uint8_t B[NSLOT] = {B_LFO, B_ENV, B_SEQ, B_ARP};
        char nm[8] = {'m', 'o', 'd', (char)('1' + s), 0};
        press(B[s]);
        shot(pal, nm);
    }
    press(B_LFO);
    turn(1, 1);                                          /* WAVE: TRIANGLE, FOLD 40, SKEW +50 */
    turn(2, 40);
    turn(3, 50);
    shot(pal, "mod1_tri_fold");
    press(B_ARP);
    turn(1, 40);                                         /* RANDOM: SMOOTH 40 */
    shot(pal, "mod4_smooth");
    press(B_HOME);
    tap(B_GLO);
    track[1].mute = 1;                                   /* track 2 muted, track 1 sounding */
    track_rt[0].peak = 9000;
    track_rt[2].peak = 1200;
    shot(pal, "mixer");
    hold(B_EDIT);                                        /* the channel: LOW +6, HIGH -4, a low-pass, PAN right */
    turn(0, 6);
    turn(1, -4);
    turn(2, -55);
    turn(3, 30);
    shot(pal, "mixer_channel");
    let_go(B_EDIT);
    shot(pal, "mixer_eq");
    tap(B_GLO);
    hold(B_GLO);
    shot(pal, "mixer_held");
    let_go(B_GLO);
    press(B_PLAY);
    press(B_REC);
    shot(pal, "playing");
    ui_message("SAVE: PROJECTS ARRIVE IN PHASE 8");
    shot(pal, "message");
    ui.msg_t = 0;
    ui.uboot = 3;
    lcd_fill(0, 0, 240, 240, T_BG);
    draw_head();
    shot(pal, "uboot");
}

int main(int argc, char **argv)
{
    uint32_t p;
    out_dir = argc > 1 ? argv[1] : "build/bryo_ui";
    test_audio();
    test_input();
    test_settings();
    for (p = 0; p < NPALETTES; p++) {
        palette_set(p);
        screens_in(UI_PALETTES[p].name);
    }
    palette_set(UI_GREY_INDEX);
    printf(fails ? "bryo: %d FAILED\n" : "bryo: all passed\n", fails);
    return fails != 0;
}

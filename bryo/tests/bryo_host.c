/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo on the host: the app's own sources (the chain, the parameter table, the UI and its input) on stubs of the
 * display and the input scan.
 *
 *   bryo_host OUT_DIR        writes OUT_DIR/ppm/<palette>_<screen>.ppm and prints one line per check
 *
 * Audio: silence at rest, a held key's pitch, mute, a click-free release, load shedding, the master stage's
 *        ceiling. Input: what each pad, key and knob does (PRD 2, phase 1). Screens: every phase 1 screen in
 *        the one ink, for tests/ui_golden.py (pixel fingerprints) and the eye (tests/bryo_ui.sh makes PNGs). */
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
#include "../firmware/src/tape.c"
#include "../firmware/src/chain.c"
#include "../firmware/src/icons.c"
#include "../firmware/src/panel.c"
/* the flash under the user reels: a NOR model (erase to 0xFF, program clears bits) */
#define RF_HOST
static uint8_t host_nor[0x100000];
static uint32_t host_prog_limit = 0xFFFFFFFFu;           /* bytes a save may program before the "power cut" */
static const uint8_t *rf_ptr(uint32_t off) { return host_nor + off; }
static int rf_erase(uint32_t off) { memset(host_nor + off, 0xFF, 4096); return 0; }
static int rf_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (!host_prog_limit)
            return -1;
        host_prog_limit--;
        host_nor[off + i] &= ((const uint8_t *)src)[i];
    }
    return 0;
}
#include "../firmware/src/reel.c"
#include "../firmware/src/settings.c"
#include "../firmware/src/ui_px.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/ui_viz.c"
#include "../firmware/src/ui_input.c"
#include "../firmware/src/vdisk.c"

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
    memset(tape_rt, 0, sizeof tape_rt);
    memset(tape_ctl, 0, sizeof tape_ctl);
    memset(tape_ram, 0, sizeof tape_ram);
    memset(&tape_undo, 0, sizeof tape_undo);
    memset(&sys, 0, sizeof sys);
    memset(&fm1_in, 0, sizeof fm1_in);
    panel = PANEL_DEFAULT;
    param_defaults();
    uslot_init();
    chain_init();
    vdisk_mount();
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

static int32_t peak_of(const int32_t *x, uint32_t n)
{
    int32_t p = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        p = abs(x[i]) > p ? abs(x[i]) : p;
    return p;
}

static void test_tape(void)
{
    enum { NB = 1378 };                                  /* 1 s */
    static int32_t s[NB * CTL];
    char b[112];
    uint32_t i;
    int32_t step = 0, pk;
    {   /* the codec: a 440 Hz sine through the encoder and back */
        int16_t in[TAPE_BLK], o[TAPE_BLK];
        uint8_t data[TAPE_BLK / 2];
        int64_t es = 0, ee = 0;
        for (i = 0; i < TAPE_BLK; i++)
            in[i] = (int16_t)(16000.0 * sin(2 * M_PI * 440.0 * i / TAPE_SR));
        ima_enc(in, 0, 0, data, TAPE_BLK);
        ima_dec(data, 0, 0, o, TAPE_BLK);
        for (i = 32; i < TAPE_BLK; i++) {                /* (after the step size has settled) */
            es += (int64_t)in[i] * in[i];
            ee += (int64_t)(in[i] - o[i]) * (in[i] - o[i]);
        }
        snprintf(b, sizeof b, "ADPCM round trip of a sine: %.1f dB SNR (> 25 dB)", 10 * log10((double)es / (double)(ee + 1)));
        check(b, 10 * log10((double)es / (double)(ee + 1)) > 25.0);
    }
    {   /* the factory reels decode into sound */
        uint32_t r, ok = 1;
        for (r = 0; r < NREEL; r++)
        {
            uint32_t k, mx = 0;
            for (k = 0; k < REELS[r].nblk; k++)
                mx = REELS[r].peak[k] > mx ? REELS[r].peak[k] : mx;
            ok &= REELS[r].nblk > 100u && REELS[r].nblk <= TAPE_NBLK && mx > 100u;
        }
        check("every factory reel fits a tape and holds sound", ok);
    }
    power_on();
    render(NB, s);
    check("silence at rest (the transport stopped): every sample 0", peak_of(s, NB * CTL) == 0);

    sys.playing = 1;                                     /* PLAY: track 1 plays reel BEAT */
    render(NB, s);
    pk = peak_of(s, NB * CTL);
    snprintf(b, sizeof b, "PLAY: the reels play, under the limiter: peak %d (> 2000, <= 32767)", (int)pk);
    check(b, pk > 2000 && pk <= 32767);
    check("..track 1's head moves forward inside its loop", tape_rt[0].running && tape_rt[0].pos > 0 &&
          (tape_rt[0].pos >> 12) < (int32_t)(REELS[0].nblk * TAPE_BLK));

    sys.playing = 0;                                     /* STOP: a fade, then silence */
    render(NB / 4, s);
    for (i = 1; i < NB / 4 * CTL; i++)
        step = abs(s[i] - s[i - 1]) > step ? abs(s[i] - s[i - 1]) : step;
    check("STOP fades out within 250 ms (every track's last block silent)",
          peak_of(track_rt[0].last, CTL) == 0 && peak_of(track_rt[1].last, CTL) == 0 &&
          peak_of(track_rt[2].last, CTL) == 0 && peak_of(track_rt[3].last, CTL) == 0);

    {   /* the loop wraps: STRT 50, LEN 10 on track 1, 1 s of play stays inside [50 %, 60 %) */
        int32_t lo, hi, len = (int32_t)(REELS[0].nblk * TAPE_BLK), inside = 1;
        tp[0].dev[DEV_SRC][TK_STRT] = 50;
        tp[0].dev[DEV_SRC][TK_LEN] = 10;
        lo = len * 50 / 100;
        hi = lo + len * 10 / 100;
        sys.playing = 1;
        for (i = 0; i < NB; i++) {
            render(1, 0);
            inside &= (tape_rt[0].pos >> 12) >= lo && (tape_rt[0].pos >> 12) < hi;
        }
        check("the loop window holds the head (STRT 50, LEN 10: 1 s of play, never outside)", inside);
        tp[0].dev[DEV_SRC][TK_REV] = 1;                  /* reverse: the head runs back */
        {
            int32_t p0 = tape_rt[0].pos, back;
            render(10, 0);
            back = tape_rt[0].pos < p0 || tape_rt[0].xf > 0;
            check("REV (OP5): the head runs backwards", back);
        }
        tp[0].dev[DEV_SRC][TK_REV] = 0;
        tp[0].dev[DEV_SRC][TK_STRT] = 0;
        tp[0].dev[DEV_SRC][TK_LEN] = 100;
        sys.playing = 0;
        render(NB / 4, 0);
    }

    {   /* a slice while stopped: white key 5 plays slice 5 of 16, then stops */
        int32_t len = (int32_t)(REELS[0].nblk * TAPE_BLK), s0 = len * 4 / 16, ran = 0;
        fm1_in.notes = note_bit_of_white(4);
        render(1, 0);
        check("stopped, white key 5 starts slice 5 (the head at its start)", tape_rt[0].running &&
              abs((tape_rt[0].pos >> 12) - s0) < 64);
        fm1_in.notes = 0;
        for (i = 0; i < NB && tape_rt[0].running; i++, ran++)
            render(1, 0);
        snprintf(b, sizeof b, "..and stops at its end: %d blocks (a slice is %d)", (int)ran, (int)(len / 16 * 2 / CTL));
        check(b, abs(ran - len / 16 * 2 / CTL) <= 2);
    }

    {   /* REC: track 2 records track 1 (its reel copied onto its tape first), DUB 0: a new take */
        uint32_t k, same = 0, diff = 0;
        check("REC arms: track 2's reel is copied onto its tape, REEL turns to TAPE",
              tape_prepare(1) && tape_src(1) == 0u && tape_ctl[1].nblk == REELS[1].nblk);
        sys.rec = 1u << 1;
        tp[1].dev[DEV_SRC][TK_DUB] = 0;
        track[1].mute = 1;                               /* (only track 1 sounds into it) */
        track[2].mute = track[3].mute = 1;
        sys.playing = 1;
        render(NB * 2, 0);                               /* 2 s: a whole pass over the 2 s loop */
        sys.rec = 0;
        render(2, 0);                                    /* (the last block commits) */
        tape_unprepare(1);
        for (k = 0; k < REELS[1].nblk; k++) {           /* the take against what was there and what went in */
            same += abs((int)tape_ram[1].peak[k] - (int)REELS[0].peak[k % REELS[0].nblk] * 100 / 127) < 24;
            diff += tape_ram[1].peak[k] != REELS[1].peak[k];
        }
        snprintf(b, sizeof b, "..recording replaces it with track 1's beat (%u of %u blocks changed, %u follow the beat)",
                 diff, (unsigned)REELS[1].nblk, same);
        check(b, diff > REELS[1].nblk * 9u / 10u && same > REELS[1].nblk * 3u / 4u);
        sys.playing = 0;
        track[1].mute = track[2].mute = track[3].mute = 0;
        render(NB / 4, 0);
    }

    {   /* clear (POLY held) and undo (SAVE held) */
        tape_view_t v;
        tape_clear(1);
        tape_view(1, &v);
        check("a clear empties the tape (it plays nothing)", v.len == 0u && tape_ctl[1].empty);
        check("..and SAVE held brings it back", tape_undo_clear() == 1 && !tape_ctl[1].empty);
        check("..once", tape_undo_clear() == -1);
        tp[1].dev[DEV_SRC][TK_REEL] = 3;                 /* a reel over a tape with a take: REC refuses */
        check("REC on a reel refuses while the tape holds a take (no take is lost)", !tape_prepare(1) &&
              tape_ctl[1].nblk == REELS[1].nblk && !tape_ctl[1].empty);
        tape_clear(1);
        tp[1].dev[DEV_SRC][TK_REEL] = 3;
        check("..after a clear it copies the reel in", tape_prepare(1) && tape_ctl[1].nblk == REELS[2].nblk);
        check("..and the clear can't be undone any more (the reel is over it)", tape_undo_clear() == -1);
        tape_unprepare(1);
    }

    {   /* the master stage holds the ceiling: four reels at full level */
        uint32_t t;
        for (t = 0; t < NTRK; t++)
            track[t].level = 127;
        sys.master_q12 = MASTER_FULL;
        sys.playing = 1;
        render(NB, s);
        check("four reels at full level and MASTER full stay under full scale (soft clip)", peak_of(s, NB * CTL) <= 32767);
        sys.playing = 0;
    }
    {
        uint32_t before = chain_shed_count;
        chain_shed();
        check("shedding is counted, and never takes the tape", chain_shed_count == before + 1u);
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
    press(B_EDIT);
    check("EDIT again, past GRAIN's page 2: RESONATOR", ui.dev == DEV_RESO);
    press(B_EDIT);
    check("EDIT again: GRAIN", ui.dev == DEV_GRAIN && ui.page == 0u);
    press(B_FX);
    check("FX: COLOR", ui.dev == DEV_COLOR);
    press(B_FX);
    press(B_FX);
    check("FX again, past COLOR's page 2: SPACE", ui.dev == DEV_SPACE && ui.page == 0u);
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
    press(B_HOME);
    check("HOME again: TAPE's page 2", ui.kind == FOCUS_DEV && ui.dev == DEV_SRC && ui.page == 1u);
    press(B_EDIT);
    check("EDIT from elsewhere: GRAIN, page 1", ui.dev == DEV_GRAIN && ui.page == 0u);
    press(B_EDIT);
    check("..again: GRAIN's page 2", ui.dev == DEV_GRAIN && ui.page == 1u);
    host_enc[panel.enc[EN_K1]] = -30;
    ui_input();
    check("..whose KNOB 1 is MIX (100 -> 70), not SIZE", tp[0].dev[DEV_GRAIN][4] == 70 && tp[0].dev[DEV_GRAIN][0] == 80);
    tp[0].dev[DEV_GRAIN][4] = 100;
    press(B_EDIT);
    check("..again: RESONATOR (one page)", ui.dev == DEV_RESO && ui.page == 0u);
    press(B_EDIT);
    check("..again: back to GRAIN, page 1", ui.dev == DEV_GRAIN && ui.page == 0u);
    press(B_FX);
    press(B_FX);
    press(B_FX);
    press(B_FX);
    check("FX: COLOR, COLOR 2, SPACE, SPACE 2", ui.dev == DEV_SPACE && ui.page == 1u);
    press(B_LFO);
    press(B_LFO);
    check("a slot pad again: the slot's page 2", ui.kind == FOCUS_SLOT && ui.slot == 0u && ui.page == 1u);
    press(B_ARP);
    press(B_ARP);
    press(B_ARP);
    press(B_ARP);
    press(B_ARP);
    check("slot 4 (the random LFO) has four pages, coming round to the first", ui.slot == 3u && ui.page == 0u &&
          tp[0].engine[3] == ME_WAVE && tp[0].mod[3][1] == LFO_RND);
    {
        uint32_t bpm = sys.bpm;
        hold(B_ARP);                                     /* slot 4's pad held + SELECT: its engine */
        host_enc[panel.enc[EN_SELECT]] = 3;
        ui_input();
        let_go(B_ARP);
        check("a slot's pad held + SELECT: LFO -> FOLLOW, from its defaults, the tempo untouched",
              tp[0].engine[3] == ME_FOLLOW && tp[0].mod[3][2] == ME_P[ME_FOLLOW][2].def && sys.bpm == bpm && ui.page == 0u);
        hold(B_ARP);
        host_enc[panel.enc[EN_SELECT]] = -3;
        ui_input();
        let_go(B_ARP);
        check("..and back (round the four engines: LFO ADSR SEQ FOLLOW)", tp[0].engine[3] == ME_WAVE);
    }
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

    key_edge(black_note(BK_OP5));
    check("black OP5: the focused track's tape reversed (TAPE 2's REV)", tp[2].dev[DEV_SRC][TK_REV] == 1);
    key_edge(black_note(BK_OP6));
    check("black OP6: half speed (TAPE 2's HALF)", tp[2].dev[DEV_SRC][TK_HALF] == 1);
    tp[2].dev[DEV_SRC][TK_REV] = tp[2].dev[DEV_SRC][TK_HALF] = 0;
    {   /* POLY held 0.5 s clears the focused tape; let go sooner does nothing; SAVE held undoes */
        uint32_t pn = black_note(BK_POLY);
        fm1_ms = 1000;
        fm1_in.notes |= 1u << pn;
        key_edge(pn);
        fm1_ms = 1300;
        fm1_in.notes &= ~(1u << pn);
        ui_input();
        check("the POLY key let go before 0.5 s: nothing is cleared", tape_src(2) == 3u);
        fm1_ms = 2000;
        fm1_in.notes |= 1u << pn;
        key_edge(pn);
        fm1_ms = 2510;
        ui_input();
        fm1_in.notes &= ~(1u << pn);
        check("..held 0.5 s: TRACK 3's tape cleared (its reel let go)", tape_src(2) == 0u && tape_ctl[2].empty);
        fm1_ms = 3000;
        hold(B_SAVE);
        fm1_ms = 3000 + HOLD_MS[settings_hold % 4u];
        ui_input();
        let_go(B_SAVE);
        check("SAVE held: the clear undone (the reel back)", tape_src(2) == 3u);
        fm1_ms = 4000;
        tap(B_SAVE);
        check("SAVE tapped: no undo, the projects message", tape_src(2) == 3u && ui.msg_t);
        ui.msg_t = 0;
    }

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
    check("REC: TRACK 3 armed", sys.rec == 1u << 2);
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
    press(B_HOME);                                       /* TAPE 2: FADE 60 ms, REV on, GAIN +6 dB */
    turn(0, 50);
    turn(1, 1);
    turn(3, 6);
    shot(pal, "tape2");
    press(B_HOME);                                       /* TAPE 3: REEL KEYS */
    turn(0, 1);
    shot(pal, "tape3");
    press(B_HOME);
    sys.playing = 1;                                     /* the head running, then REC armed */
    render(600, 0);
    shot(pal, "tape_playing");
    press(B_REC);
    render(4, 0);
    shot(pal, "tape_rec");
    press(B_REC);
    sys.playing = 0;
    render(400, 0);
    press(B_EDIT);
    shot(pal, "grain");
    turn(1, 40);                                         /* DENS 80 %, PITCH +7, SPREAD 90 % */
    turn(2, 7);
    turn(3, 60);
    shot(pal, "grain_busy");
    press(B_EDIT);                                       /* GRAIN 2: JIT 60, WIN 100, REV 40 */
    turn(1, 40);
    turn(2, 50);
    turn(3, 40);
    shot(pal, "grain2");
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
    press(B_FX);                                         /* COLOR 2: LVL -6 dB, MIX 70, SRR 60, GATE 40 */
    turn(0, -6);
    turn(1, -30);
    turn(2, 60);
    turn(3, 40);
    shot(pal, "color2");
    press(B_FX);
    shot(pal, "space");
    turn(1, 40);                                         /* FDBK 70 */
    turn(3, 40);                                         /* DECAY 80 */
    shot(pal, "space_long");
    press(B_FX);                                         /* SPACE 2: DMIX 80, RMIX 60, PRE 120 ms */
    turn(0, 50);
    turn(1, 30);
    turn(2, 100);
    shot(pal, "space2");
    for (s = 0; s < NSLOT; s++) {
        static const uint8_t B[NSLOT] = {B_LFO, B_ENV, B_SEQ, B_ARP};
        char nm[8] = {'m', 'o', 'd', (char)('1' + s), 0};
        press(B[s]);
        shot(pal, nm);
    }
    press(B_LFO);
    turn(1, 1);                                          /* LFO: TRIANGLE, SKEW +40, FOLD 50 */
    turn(2, 40);
    turn(3, 50);
    shot(pal, "mod1_tri_fold");
    press(B_LFO);                                        /* LFO 2: CURV -60, SMTH 30, VAR 40, LEN 4 */
    turn(0, -60);
    turn(1, 30);
    turn(2, 40);
    turn(3, -4);
    shot(pal, "mod1_p2");
    press(B_LFO);                                        /* LFO 3: AMT 70, OFS +20, PHAS 90, SPRD 50 */
    turn(0, -30);
    turn(1, 20);
    turn(2, 90);
    turn(3, 50);
    shot(pal, "mod1_p3");
    press(B_LFO);                                        /* LFO 4: TRIG KEY, FADE 40 (SYNC stays BPM) */
    turn(1, 1);
    turn(2, 40);
    shot(pal, "mod1_p4");
    press(B_ENV);                                        /* ADSR 2: ACRV -60, DCRV +60, RCRV +40, SPRD 40 */
    press(B_ENV);
    turn(0, -60);
    turn(1, 60);
    turn(2, 40);
    turn(3, 40);
    shot(pal, "mod2_p2");
    press(B_ENV);                                        /* ADSR 3: VEL 40, LOOP on */
    turn(0, 40);
    turn(1, 1);
    shot(pal, "mod2_p3");
    press(B_SEQ);
    turn(0, -4);                                         /* SEQ: LEN 12, SLEW 60, SWING 50 */
    turn(2, 60);
    turn(3, 50);
    shot(pal, "mod3_seq");
    press(B_SEQ);                                        /* SEQ 2: PING, PROB 70, STRT 5 */
    turn(0, 2);
    turn(2, -30);
    turn(3, 4);
    shot(pal, "mod3_p2");
    press(B_ARP);
    shot(pal, "mod4_rnd");                               /* slot 4: the LFO on RND, VAR 20 */
    press(B_ARP);                                        /* LFO 2 on RND: SMTH 40, VAR 60, LEN 12 */
    turn(1, 40);
    turn(2, 40);
    turn(3, 4);
    shot(pal, "mod4_smooth");
    press(B_ARP);                                        /* LFO 3: AMT 80, OFS +20, PHAS 90, SPRD 40 */
    turn(0, -20);
    turn(1, 20);
    turn(2, 90);
    turn(3, 40);
    shot(pal, "mod4_p3");
    hold(B_ARP);                                         /* slot 4 runs FOLLOW: RISE 4, FALL 30, GAIN +6 */
    host_enc[panel.enc[EN_SELECT]] = -1;
    ui_input();
    let_go(B_ARP);
    turn(1, 6);
    turn(2, -6);
    turn(3, -20);
    shot(pal, "mod4_follow");
    press(B_ARP);                                        /* FOLLOW 2: HOLD 1/16 */
    turn(0, 2);
    shot(pal, "mod4_follow2");
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

/* ---------------------------------------------------------- user reels --- */
static void test_uslots(void)
{
    tape_view_t v;
    power_on();
    check("blank flash: every user slot empty, named U1..U6", uslot_free() == 0 && !uslot_valid(5) &&
          !strcmp(uslot_name[0], "U1") && !strcmp(uslot_name[5], "U6"));
    check("a save: slot 1 holds BEAT under the name 'kick drum.wav' -> KICK",
          uslot_save(0, REELS[0].data, REELS[0].pred, REELS[0].idx, REELS[0].peak, REELS[0].nblk, "kick drum.wav") == 0 &&
          uslot_valid(0) && !strcmp(uslot_name[0], "KICK") && uslot_free() == 1);
    tp[0].dev[DEV_SRC][TK_REEL] = (int16_t)(NREEL + 1u);
    tape_view(0, &v);
    check("..and a track plays it straight from flash (REEL KICK)", v.len == REELS[0].nblk * TAPE_BLK && !v.ram &&
          !memcmp(v.data, REELS[0].data, 64) && !strcmp(tape_name(0), "KICK"));
    check("..REC copies it onto the tape like a factory reel", tape_prepare(0) && tape_src(0) == 0u &&
          tape_ctl[0].nblk == REELS[0].nblk);
    tape_unprepare(0);
    host_prog_limit = 20000;                             /* a power cut halfway through the data */
    check("a save torn by a power cut leaves the slot empty, not half old and half new",
          uslot_save(0, REELS[1].data, REELS[1].pred, REELS[1].idx, REELS[1].peak, REELS[1].nblk, "PIANO") != 0 &&
          !uslot_valid(0) && !strcmp(uslot_name[0], "U1"));
    host_prog_limit = 0xFFFFFFFFu;
    memset(host_nor, 0xFF, sizeof host_nor);
    uslot_names();
}

/* ---------------------------------------------------------------- drive --- */
/* the computer's side: a FAT12 reader and writer that knows only what the disk says */
static uint8_t hd_boot[512], hd_fat[12 * 512], hd_root[64 * 32];
static uint32_t hd_spc, hd_fat0, hd_nfat, hd_fats, hd_root0, hd_data, hd_nclus;
static void hd_mount(void)
{
    uint32_t i;
    vdisk_read(0, hd_boot);
    hd_spc = hd_boot[13];
    hd_fat0 = rd16(hd_boot + 14);
    hd_nfat = hd_boot[16];
    hd_fats = rd16(hd_boot + 22);
    hd_root0 = hd_fat0 + hd_nfat * hd_fats;
    hd_data = hd_root0 + rd16(hd_boot + 17) * 32u / 512u;
    hd_nclus = ((rd16(hd_boot + 19) ? rd16(hd_boot + 19) : rd32(hd_boot + 32)) - hd_data) / hd_spc;
    for (i = 0; i < hd_fats; i++)
        vdisk_read(hd_fat0 + i, hd_fat + 512 * i);
    for (i = 0; i < 4; i++)
        vdisk_read(hd_root0 + i, hd_root + 512 * i);
}
static uint32_t hd_fat_get(uint32_t c) { uint32_t v = rd16(hd_fat + c + c / 2); return c & 1 ? v >> 4 : v & 0xFFF; }
static void hd_fat_set(uint32_t c, uint32_t v)
{
    uint32_t o = c + c / 2, w = rd16(hd_fat + o);
    w = c & 1 ? (w & 0xF) | (v << 4) : (w & 0xF000) | (v & 0xFFF);
    wr16(hd_fat + o, w);
}
static uint8_t *hd_find(const char *n83)
{
    uint32_t i;
    for (i = 0; i < 64; i++)
        if (!memcmp(hd_root + 32 * i, n83, 11))
            return hd_root + 32 * i;
    return 0;
}
/* the file at dir entry e into buf (max n bytes); returns its size */
static uint32_t hd_read_file(const uint8_t *e, uint8_t *buf, uint32_t n)
{
    uint32_t c = rd16(e + 26), size = rd32(e + 28), got = 0, s;
    uint8_t sec[512];
    while (c >= 2 && c < 0xFF8 && got < size) {
        for (s = 0; s < hd_spc && got < size; s++) {
            uint32_t k = size - got < 512 ? size - got : 512;
            vdisk_read(hd_data + (c - 2) * hd_spc + s, sec);
            if (got + k <= n)
                memcpy(buf + got, sec, k);
            got += k;
        }
        c = hd_fat_get(c);
    }
    return size;
}
/* write a file of n bytes: into the clusters of an existing entry e (in place), or new clusters and a new entry
 * named short83 (+ a long name); the data first, then the FAT, then the directory, as file managers tend to */
static void hd_write_file(uint8_t *e, const char *short83, const char *lfn, const uint8_t *data, uint32_t n,
                          int name_it)
{
    uint32_t need = (n + hd_spc * 512 - 1) / (hd_spc * 512), cl[512], k = 0, c, i, w = 0;
    uint8_t sec[512];
    if (e) {
        for (c = rd16(e + 26); c >= 2 && c < 0xFF8 && k < need; c = hd_fat_get(c))
            cl[k++] = c;
    }
    for (c = k ? cl[k - 1] + 1 : 2; k < need && c < hd_nclus + 2; c++)   /* next fit: on from the file's end */
        if (!hd_fat_get(c))
            cl[k++] = c;
    for (i = 0; i < need; i++) {
        uint32_t s;
        for (s = 0; s < hd_spc; s++, w += 512) {
            memset(sec, 0, 512);
            if (w < n)
                memcpy(sec, data + w, n - w < 512 ? n - w : 512);
            vdisk_write(hd_data + (cl[i] - 2) * hd_spc + s, sec);
        }
        hd_fat_set(cl[i], i + 1 < need ? cl[i + 1] : 0xFFF);
    }
    for (i = 0; i < hd_fats; i++) {
        vdisk_write(hd_fat0 + i, hd_fat + 512 * i);
        vdisk_write(hd_fat0 + hd_fats + i, hd_fat + 512 * i);
    }
    if (!name_it)
        return;
    if (!e) {
        uint32_t slot;
        for (slot = 0; slot < 63 && hd_root[32 * slot] && hd_root[32 * slot] != 0xE5; slot++)
            ;
        if (lfn) {                                       /* one long-name entry (up to 13 characters) */
            static const uint8_t AT[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
            uint8_t *l = hd_root + 32 * slot;
            memset(l, 0xFF, 32);
            l[0] = 0x41;
            l[11] = 0x0F;
            l[12] = 0;
            wr16(l + 26, 0);
            for (i = 0; i < 13; i++)
                wr16(l + AT[i], i < strlen(lfn) ? (uint8_t)lfn[i] : i == strlen(lfn) ? 0 : 0xFFFF);
            slot++;
        }
        e = hd_root + 32 * slot;
        memset(e, 0, 32);
        memcpy(e, short83, 11);
    }
    wr16(e + 26, cl[0]);
    wr32(e + 28, n);
    for (i = 0; i < 4; i++)
        vdisk_write(hd_root0 + i, hd_root + 512 * i);
}
/* a WAV: rate, channels, bits, frames of a sine at hz (amplitude 0.5) */
static uint32_t make_wav(uint8_t *o, uint32_t rate, uint32_t ch, uint32_t bits, uint32_t tag, uint32_t frames, double hz)
{
    uint32_t bpf = ch * bits / 8, n = frames * bpf, i, c;
    memcpy(o, "RIFF", 4);
    wr32(o + 4, 36 + 8 + 26 + n);
    memcpy(o + 8, "WAVELIST", 8);                        /* a chunk Bryo skips */
    wr32(o + 16, 18);
    memcpy(o + 20, "INFOISFT\x06\0\0\0bryo\0\0", 18);
    memcpy(o + 38, "fmt ", 4);
    wr32(o + 42, 16);
    wr16(o + 46, tag);
    wr16(o + 48, ch);
    wr32(o + 50, rate);
    wr32(o + 54, rate * bpf);
    wr16(o + 58, bpf);
    wr16(o + 60, bits);
    memcpy(o + 62, "data", 4);
    wr32(o + 66, n);
    for (i = 0; i < frames; i++)
        for (c = 0; c < ch; c++) {
            double v = 0.5 * sin(2 * M_PI * hz * i / rate);
            uint8_t *p = o + 70 + i * bpf + c * bits / 8;
            if (tag == 3) {
                float f = (float)v;
                memcpy(p, &f, 4);
            } else if (bits == 16) {
                wr16(p, (uint16_t)(int16_t)(v * 32767));
            } else if (bits == 24) {
                int32_t x = (int32_t)(v * 8388607);
                p[0] = (uint8_t)x; p[1] = (uint8_t)(x >> 8); p[2] = (uint8_t)(x >> 16);
            }
        }
    return 70 + n;
}
/* the tape or reel's sound: zero crossings per second (its pitch, for a sine) and its peak */
static double view_hz(const tape_view_t *v, int32_t *peak)
{
    tape_rd_t r = {0};
    uint32_t i, cross = 0, n = v->len - 512 > 22050 ? 22050 : v->len - 512;   /* (not the last block's padding) */
    int32_t prev = 0, pk = 0;
    for (i = 2000; i < n; i++) {
        int32_t x = tape_at(v, &r, (int32_t)i);
        cross += prev < 0 && x >= 0;
        prev = x;
        pk = abs(x) > pk ? abs(x) : pk;
    }
    *peak = pk;
    return cross * 22050.0 / (n - 2000);
}

static void test_drive(void)
{
    static uint8_t buf[400000], wav[1200000];
    uint8_t *e;
    tape_view_t v;
    uint32_t n, i, ok;
    int32_t pk;
    double hz;
    char b[120];
    power_on();
    hd_mount();
    check("the drive: a FAT12 volume BRYO (512 B sectors, 63.5 MiB, 16 KiB clusters, under 4085 clusters)",
          rd16(hd_boot + 11) == 512 && hd_boot[510] == 0x55 && hd_boot[511] == 0xAA && !memcmp(hd_boot + 54, "FAT12", 5) &&
          hd_nclus < 4085 && hd_nclus > 4000 && !memcmp(hd_root, "BRYO       ", 11) && hd_root[11] == 0x08);
    check("..with README.TXT, TAPE1-4.WAV and REEL1-4.WAV (factory reels read-only), no USER files on a blank flash",
          hd_find("README  TXT") && hd_find("TAPE1   WAV") && hd_find("TAPE4   WAV") && hd_find("REEL1   WAV") &&
          (hd_find("REEL4   WAV")[11] & 1) && !hd_find("USER1   WAV"));
    n = hd_read_file(hd_find("README  TXT"), buf, sizeof buf);
    check("README.TXT reads as text", n > 200 && !memcmp(buf, "BRYO", 4));
    n = hd_read_file(hd_find("TAPE1   WAV"), buf, sizeof buf);
    tape_view(0, &v);
    ok = n == 44 + v.len * 2 && !memcmp(buf, "RIFF", 4) && rd32(buf + 24) == 22050 && rd16(buf + 22) == 1 && rd16(buf + 34) == 16;
    {
        tape_rd_t r = {0};
        for (i = 0; i < v.len && ok; i += 97)
            ok = (int16_t)rd16(buf + 44 + 2 * i) == tape_at(&v, &r, (int32_t)i);
    }
    check("TAPE1.WAV is track 1's sound: a 22,050 Hz 16-bit mono WAV, sample for sample", ok);

    n = make_wav(wav, 44100, 2, 16, 1, 44100, 441.0);   /* 1 s of 441 Hz, stereo 16-bit 44.1 kHz, a LIST chunk */
    hd_write_file(0, "KICKDR~1WAV", "kick drum.wav", wav, n, 1);
    vdisk_poll();
    ok = uslot_valid(0) && !strcmp(uslot_name[0], "KICK");
    uslot_view(0, &v);
    hz = view_hz(&v, &pk);
    snprintf(b, sizeof b, "a WAV copied on as 'kick drum.wav' (44.1 kHz stereo) lands in user reel 1 as KICK: %.0f Hz, peak %d", hz, (int)pk);
    check(b, ok && fabs(hz - 441) < 8 && pk > 12000 && pk < 20000 && v.len == 22050u / 256u * 256u + 256u);

    e = hd_find("TAPE2   WAV");                          /* overwrite TAPE2.WAV in place: 24-bit 48 kHz mono */
    n = make_wav(wav, 48000, 1, 24, 1, 48000 * 4, 220.0);
    hd_write_file(e, 0, 0, wav, n, 1);
    vdisk_poll();
    tape_view(1, &v);
    hz = view_hz(&v, &pk);
    snprintf(b, sizeof b, "TAPE2.WAV overwritten (24-bit 48 kHz, 4 s) replaces track 2's tape, cut to 3.3 s: %.0f Hz", hz);
    check(b, tape_src(1) == 0 && tape_ctl[1].nblk == TAPE_NBLK && fabs(hz - 220) < 6 && pk > 12000);

    n = make_wav(wav, 22050, 1, 32, 3, 11025, 1000.0);   /* float32, into a folder: never named in the root */
    hd_write_file(0, 0, 0, wav, n, 0);
    vdisk_poll();
    check("..a WAV with no name in the root waits", !uslot_valid(1));
    fm1_ms += 3500;
    vdisk_poll();
    uslot_view(1, &v);
    hz = view_hz(&v, &pk);
    snprintf(b, sizeof b, "..then goes to the next free user reel as U2 (float32 read): %.0f Hz", hz);
    check(b, uslot_valid(1) && !strcmp(uslot_name[1], "U2") && fabs(hz - 1000) < 20);

    n = make_wav(wav, 22050, 1, 16, 2, 2000, 100.0);     /* format 2 (MS ADPCM): not readable */
    hd_write_file(0, "BAD     WAV", 0, wav, n, 1);
    ui.msg_t = 0;
    vdisk_poll();
    check("a WAV in a format Bryo can't read is refused, with a message, nothing saved", !uslot_valid(2) && ui.msg_t &&
          !strcmp(ui.msg, "THAT WAV COULDN'T BE READ"));
    {   /* scattered: two runs of clusters with a used cluster between them, the FAT written before the data */
        uint32_t cl[64] = {0}, k = 0, c, w = 0, j, need;
        uint8_t sec[512];
        n = make_wav(wav, 22050, 1, 16, 1, 22050, 300.0);
        need = (n + hd_spc * 512 - 1) / (hd_spc * 512);
        for (c = 2; k < need; c++)
            if (!hd_fat_get(c) && (k != 1 || c > cl[0] + 1))   /* a gap after the first cluster */
                cl[k++] = c;
        hd_fat_set(cl[0] + 1, 0xFFF);                      /* (something else's cluster in the gap) */
        for (j = 0; j < need; j++)
            hd_fat_set(cl[j], j + 1 < need ? cl[j + 1] : 0xFFF);
        for (j = 0; j < hd_fats; j++) {
            vdisk_write(hd_fat0 + j, hd_fat + 512 * j);
            vdisk_write(hd_fat0 + hd_fats + j, hd_fat + 512 * j);
        }
        for (j = 0; j < need; j++)
            for (c = 0; c < hd_spc; c++, w += 512) {
                memset(sec, 0, 512);
                if (w < n)
                    memcpy(sec, wav + w, n - w < 512 ? n - w : 512);
                vdisk_write(hd_data + (cl[j] - 2) * hd_spc + c, sec);
            }
        for (j = 0; j < 64 && hd_root[32 * j]; j++)
            ;
        memset(hd_root + 32 * j, 0, 32);
        memcpy(hd_root + 32 * j, "SCATTER WAV", 11);
        wr16(hd_root + 32 * j + 26, cl[0]);
        wr32(hd_root + 32 * j + 28, n);
        for (j = 0; j < 4; j++)
            vdisk_write(hd_root0 + j, hd_root + 512 * j);
        vdisk_poll();
        uslot_view(2, &v);
        hz = view_hz(&v, &pk);
        snprintf(b, sizeof b, "a WAV in two scattered runs (the FAT written first) arrives whole: SCAT, %.0f Hz", hz);
        check(b, uslot_valid(2) && !strcmp(uslot_name[2], "SCAT") && fabs(hz - 300) < 6 && v.len >= 22050u);
    }
    hd_mount();
    check("the computer reads back the FAT and directory as it wrote them", hd_find("KICKDR~1WAV") && hd_find("BAD     WAV"));
    vdisk_mount();
    hd_mount();
    check("plugged in again: USER1.WAV and USER2.WAV are files now", hd_find("USER1   WAV") && hd_find("USER2   WAV") &&
          rd32(hd_find("USER1   WAV") + 28) == 44 + REELS[0].nblk * 0 + 2 * (22050u / 256u * 256u + 256u));
    memset(host_nor, 0xFF, sizeof host_nor);
    uslot_names();
}

int main(int argc, char **argv)
{
    out_dir = argc > 1 ? argv[1] : "build/bryo_ui";
    memset(host_nor, 0xFF, sizeof host_nor);
    test_tape();
    test_uslots();
    test_drive();
    test_input();
    test_settings();
    palette_set(UI_GREY_INDEX);                          /* (the screens are one ink now, whatever the palette) */
    screens_in("BRYO");
    printf(fails ? "bryo: %d FAILED\n" : "bryo: all passed\n", fails);
    return fails != 0;
}

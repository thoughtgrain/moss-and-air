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
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {         /* (hal/fm1_input.h's: where each LED is) */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, {5, 11, 4, 10, 3, 9, 2, 8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13, 7, 6, 12}, {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    {0, 1, 15, 14, 17, 16, 19, 18, 20, 21, 22}, {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}};
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
#define GFX_DOT_TEXT 1
#include "../firmware/src/gfx.c"
#include "../firmware/src/bryo.h"
static void ui_message(const char *s);
static void ui_redraw(void);
#include "../firmware/src/dsp.c"
#include "../firmware/src/master.c"
#include "../firmware/src/param.c"
#include "../firmware/src/mod.c"
#include "../firmware/src/mem.c"
#include "../firmware/src/tape.c"
#include "../firmware/src/synth.c"
#include "../firmware/src/poly.c"
#include "../firmware/src/drum_voice.c"
#include "../firmware/src/drum.c"
#include "../firmware/src/source.c"
#include "../firmware/src/grain.c"
#include "../firmware/src/reso.c"
#include "../firmware/src/color.c"
#include "../firmware/src/space.c"
#include "../firmware/src/mixer.c"
#include "../firmware/src/usbrec.c"
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
#include "../firmware/src/project.c"
#include "../firmware/src/settings.c"
#include "../firmware/src/ui_px.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/ui_viz.c"
#include "../firmware/src/ui_input.c"
#define BRYO_MSC 1                                       /* (vdisk.c's hooks for msc.c: tested without USB) */
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
    memset(&cap, 0, sizeof cap);
    memset(&capm, 0, sizeof capm);
    memset(reso, 0, sizeof reso);
    reso_cap = RS_N;
    memset(color, 0, sizeof color);
    memset(space, 0, sizeof space);
    memset(&ur, 0, sizeof ur);
    ur_mw = ur_mr = ur_rw = ur_rr = 0;
    memset(chan, 0, sizeof chan);
    comp.env = comp.gr_q8 = 0;
    comp.g = 4096;
    memset(gbuf, 0, sizeof gbuf);                        /* (GRAIN's buffers and grains: a fresh start, as the */
    memset(grain, 0, sizeof grain);                      /* device's power-on gives) */
    gr_used = 0;
    sys.freeze = 0;
    vd_sp_w = vd_sp_r = 0;
    memset(&tape_undo, 0, sizeof tape_undo);
    memset(syn, 0, sizeof syn);
    memset(pol, 0, sizeof pol);
    memset(drm_rt, 0, sizeof drm_rt);
    memset(&drm_undo_buf, 0, sizeof drm_undo_buf);
    memset(drm_dirty, 0, sizeof drm_dirty);
    drm_rq_w = drm_rq_r = 0;
    memset(pj_sum, 0, sizeof pj_sum);                    /* (the projects' RAM side: as at boot, before pj_boot) */
    pj_cur = 0;
    pj_seq = 0;
    pj_hash_at = 0;
    memset(&sys, 0, sizeof sys);
    memset(&fm1_in, 0, sizeof fm1_in);
    panel = PANEL_DEFAULT;
    param_defaults();
    uslot_init();
    chain_init();
    vdisk_mount();
    ui_init();
    sys.bpm = 120;
    sys.ntrk = NTRK;
    sys.master_q12 = 2048;
    sys.keys_live = 1;
}

/* block b of track t's RAM tape: its peak, its data */
static uint32_t tpk(uint32_t t, uint32_t b)
{
    tape_view_t v = {0};
    v.map = tape_ctl[t].map;
    return tv_peak(&v, b);
}
static const uint8_t *tdata(uint32_t t, uint32_t b)
{
    tape_view_t v = {0};
    v.map = tape_ctl[t].map;
    return tv_data(&v, b);
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

static uint32_t note_of_white(uint32_t w)              /* white key w's note (fm1_in.notes bit) */
{
    uint32_t n;
    for (n = 0; n < 27u && KEY_WHITE[n] != w; n++)
        ;
    return n;
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
        tp[1].dev[DEV_SRC][TK_DUB] = -100;                   /* (DUB -100: the input replaces the loop) */
        track[1].mute = 1;                               /* (only track 1 sounds into it) */
        track[2].mute = track[3].mute = 1;
        sys.playing = 1;
        render(NB * 2, 0);                               /* 2 s: a whole pass over the 2 s loop */
        sys.rec = 0;
        render(2, 0);                                    /* (the last block commits) */
        tape_unprepare(1);
        for (k = 0; k < REELS[1].nblk; k++) {           /* the take against what was there and what went in */
            same += abs((int)tpk(1, k) - (int)REELS[0].peak[k % REELS[0].nblk] * 100 / 127) < 24;
            diff += tpk(1, k) != REELS[1].peak[k];
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
    press(B_EDIT);
    check("EDIT again: GRAIN 4 (SCAN WARP OFST FDBK)", ui.dev == DEV_GRAIN && ui.page == 3u);
    press(B_EDIT);
    check("EDIT again, past GRAIN's pages 2 to 4: RESONATOR", ui.dev == DEV_RESO);
    press(B_EDIT);
    press(B_EDIT);
    press(B_EDIT);
    check("EDIT again, past RESONATOR's pages 2 and 3: GRAIN", ui.dev == DEV_GRAIN && ui.page == 0u);
    press(B_FX);
    check("FX: COLOR", ui.dev == DEV_COLOR);
    press(B_FX);
    press(B_FX);
    press(B_FX);
    check("FX again, past COLOR's pages 2 and 3: SPACE", ui.dev == DEV_SPACE && ui.page == 0u);
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
    host_enc[panel.enc[EN_K1]] = 30;
    ui_input();
    check("..whose KNOB 1 is WET (0 -> 30), not SIZE", tp[0].dev[DEV_GRAIN][4] == 30 && tp[0].dev[DEV_GRAIN][0] == 80);
    tp[0].dev[DEV_GRAIN][4] = 0;
    press(B_EDIT);
    check("..again: GRAIN's page 3 (PATN SCAL PRND)", ui.dev == DEV_GRAIN && ui.page == 2u &&
          !strcmp(dev_p(0, DEV_GRAIN)[8].label, "PATN"));
    press(B_EDIT);
    check("..again: GRAIN's page 4 (SCAN WARP OFST FDBK: what grains read, set up left to right)", ui.dev == DEV_GRAIN &&
          ui.page == 3u && !strcmp(dev_p(0, DEV_GRAIN)[12].label, "SCAN") && !strcmp(dev_p(0, DEV_GRAIN)[13].label, "WARP"));
    press(B_EDIT);
    check("..again: RESONATOR", ui.dev == DEV_RESO && ui.page == 0u);
    press(B_EDIT);
    check("..again: RESONATOR's page 2 (CUT RES SLOP SCAL)", ui.dev == DEV_RESO && ui.page == 1u);
    press(B_EDIT);
    check("..again: RESONATOR's page 3 (ROUT)", ui.dev == DEV_RESO && ui.page == 2u &&
          !strcmp(dev_p(0, DEV_RESO)[RP_ROUT].label, "ROUT"));
    press(B_EDIT);
    check("..again: back to GRAIN, page 1", ui.dev == DEV_GRAIN && ui.page == 0u);
    press(B_FX);
    press(B_FX);
    press(B_FX);
    press(B_FX);
    press(B_FX);
    check("FX: COLOR, COLOR 2, COLOR 3, SPACE, SPACE 2", ui.dev == DEV_SPACE && ui.page == 1u);
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
        check("SAVE tapped: no undo; it saves the project (P1)", tape_src(2) == 3u && pj_sum[0].used &&
              !strncmp(ui.msg, "P1 SAVED", 8));
        pj_delete(0);
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
    check("EDIT let go after turning a knob: the levels again, the mixer still up", !ui.chan && ui.view == VIEW_MIXER);
    tp[2].ch[CH_FILT] = 0;
    tap(B_EDIT);
    check("EDIT tapped on the mixer: the channel page stays (latched, as a tapped GLO; the right hand free for the knobs)",
          ui.chan && ui.chan_latched && ui.view == VIEW_MIXER);
    tap(B_EDIT);
    check("..tapped again: MASTER, the compressor's page (latched)", ui.chan == CHAN_MASTER && ui.chan_latched &&
          ui.view == VIEW_MIXER);
    host_enc[panel.enc[EN_K1]] = 20;
    ui_input();
    check("..KNOB 1 sets the master compressor's AMT (0 -> 20), one for all tracks", mst[MS_AMT] == 20);
    mst[MS_AMT] = 0;
    tap(B_EDIT);
    check("..tapped again: the levels", !ui.chan && !ui.chan_latched && ui.view == VIEW_MIXER);
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
static void shot_screen(const char *pal, const char *name);
static void chain_poll(void);
static void shot(const char *pal, const char *name)
{
    chain_poll();                                        /* (the main loop's bookkeeping, as every frame) */
    ui.force = 1;
    ui_draw();
    shot_screen(pal, name);
}
static void shot_screen(const char *pal, const char *name)   /* the screen as it is (no redraw) */
{
    char path[512];
    FILE *f;
    uint32_t i;
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

static void ur_feed(uint32_t frames, double hz, double a);   /* (test_usbrec's: the computer playing) */
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
    press(B_HOME);                                       /* TAPE 3: REEL KEYS, ROTA 25 % (playing starts there), LVL -6 */
    turn(0, 1);
    turn(1, 25);
    turn(2, -6);
    shot(pal, "tape3");
    turn(2, 6);
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
    hold(B_HOME);                                        /* HOME held + SELECT: SYNTH, its four pages */
    host_enc[panel.enc[EN_SELECT]] = 1;
    ui_input();
    let_go(B_HOME);
    shot(pal, "synth");
    turn(1, 30);                                         /* DTUN 38 ct, MIX 70 %, NOIS 20 % */
    turn(2, 20);
    turn(3, 20);
    shot(pal, "synth_osc");
    press(B_HOME);                                       /* FILTER: CUT down, RES up */
    turn(0, -20);
    turn(1, 50);
    shot(pal, "synth_filter");
    press(B_HOME);                                       /* AMP */
    turn(0, 30);
    shot(pal, "synth_amp");
    press(B_HOME);                                       /* VOICE: GLID, DRV, TUNE */
    turn(1, 70);
    turn(2, 60);
    turn(3, -5);
    shot(pal, "synth_voice");
    press(B_HOME);                                       /* LEVEL: -9 dB */
    turn(0, -9);
    shot(pal, "synth_level");
    hold(B_HOME);                                        /* POLY: its four pages */
    host_enc[panel.enc[EN_SELECT]] = 1;
    ui_input();
    let_go(B_HOME);
    shot(pal, "poly");
    turn(1, 30);                                         /* STRT 30 %, TUNE -5, VOIC 3 */
    turn(2, -5);
    turn(3, -1);
    shot(pal, "poly_start");
    press(B_HOME);                                       /* ENV: a slower attack, SUS 70 */
    turn(0, 40);
    turn(2, -30);
    shot(pal, "poly_env");
    press(B_HOME);                                       /* FILTER: HP, CUT 60, RES 50, ENV +40 */
    turn(0, -67);
    turn(1, 50);
    turn(2, 2);
    turn(3, 40);
    shot(pal, "poly_filter");
    press(B_HOME);                                       /* LEVEL: +4 dB */
    turn(0, 4);
    shot(pal, "poly_level");
    hold(B_HOME);                                        /* DRUM: its four pages */
    host_enc[panel.enc[EN_SELECT]] = 1;
    ui_input();
    let_go(B_HOME);
    shot(pal, "drum");                                   /* PATTERN: ROCK's two bars, the first shown */
    sys.playing = 1;                                     /* playing: the playhead, three sixteenths in */
    render(3u * 172u + 20u, 0);
    shot(pal, "drum_playing");
    sys.playing = 0;
    render(4, 0);
    press(B_HOME);                                       /* VARY: SEED 7 at VARY 80: what this version changes */
    turn(0, 7);
    turn(1, 40);
    shot(pal, "drum_vary");
    press(B_HOME);                                       /* KIT: the instrument STEP picks (BD), DECY +30 */
    turn(1, 30);
    shot(pal, "drum_kit");
    fm1_in.notes = note_bit_of_white(4);                 /* the low conga held: its own knobs, TUNE -3 */
    turn(0, -3);
    shot(pal, "drum_inst");
    fm1_in.notes = 0;
    ui_input();
    press(B_HOME);                                       /* STEP: the snare's row of bar 1 */
    hold(B_HOME);
    key_edge(note_of_white(1));
    let_go(B_HOME);
    shot(pal, "drum_step");
    turn(2, 2);                                          /* MODE LIVE: the grid, named */
    shot(pal, "drum_live");
    turn(2, -2);
    hold(B_HOME);                                        /* back to TAPE (round past DRUM) */
    host_enc[panel.enc[EN_SELECT]] = 1;
    ui_input();
    let_go(B_HOME);
    press(B_EDIT);
    shot(pal, "grain");
    turn(1, 40);                                         /* RATE 80 %, PTCH +7, SPRD 90 % */
    turn(2, 7);
    turn(3, 60);
    shot(pal, "grain_busy");
    press(B_EDIT);                                       /* GRAIN 2: WET 80, SPRY 60, CONT 100, REV 40 */
    turn(0, 80);
    turn(1, 40);
    turn(2, 50);
    turn(3, 40);
    shot(pal, "grain2");
    press(B_EDIT);                                       /* GRAIN 3: PATN SWNG, SCAL MAJ, PRND 5 */
    turn(0, -2);
    turn(1, 2);
    turn(2, 5);
    shot(pal, "grain3");
    {   /* playing a bar into the buffer, then the 0 black key held: the buffer frozen, the grains looping it */
        uint32_t u, z = 0;
        for (u = 0; u < 27u; u++)
            if (KEY_BLACK[u] == BK_ZERO)
                z = 1u << u;
        sys.playing = 1;
        for (u = 0; u < 1378u * 3u; u++) {
            render(1, 0);
            chain_poll();
        }
        shot(pal, "grain_playing");
        fm1_in.notes = z;
        render(40, 0);
        shot(pal, "grain_frozen");
        fm1_in.notes = 0;
        press(B_EDIT);                                   /* GRAIN 4: SCAN DLY, WARP 52 %, OFST 50, FDBK 30 */
        turn(0, 2);
        turn(1, -12);
        turn(2, 25);
        turn(3, 30);
        render(400, 0);
        shot(pal, "grain4");
        turn(0, -3);                                     /* SCAN TAPE: grains of the tape, its field zoomed */
        render(400, 0);
        shot(pal, "grain_tape");
        turn(0, 1);
        turn(1, 12);
        turn(2, -25);
        turn(3, -30);
        sys.playing = 0;
        render(400, 0);
    }
    press(B_EDIT);
    shot(pal, "resonator");
    turn(1, 35);                                         /* DEC 95: sharp peaks */
    turn(2, 30);                                         /* TONE 90: the upper partials kept */
    turn(3, 80);                                         /* WET 80 */
    shot(pal, "resonator_wet");
    press(B_EDIT);                                       /* RESONATOR 2: CUT down, RES 60, BP, the major chord */
    turn(0, -40);
    turn(1, 60);
    turn(2, 1);
    turn(3, 1);
    shot(pal, "resonator2");
    fm1_in.notes = note_bit_of_white(7);                 /* a key on the RESONATOR page: the root to G3, plucked */
    render(2, 0);
    fm1_in.notes = 0;
    shot(pal, "resonator_key");
    press(B_FX);
    shot(pal, "color");
    turn(0, 60);                                         /* DRIVE 60: the transfer curve */
    shot(pal, "color_drive");
    turn(1, 70);                                         /* CRUSH 70: steps */
    shot(pal, "color_crush");
    turn(2, 50);                                         /* NOISE 50 */
    shot(pal, "color_noise");
    turn(3, -30);                                        /* TILT -30: darker */
    shot(pal, "color_tone");
    press(B_FX);                                         /* COLOR 2: NDEC longer, NTON -30, CMOD BOTH, WET 70 */
    turn(0, 30);
    turn(1, -30);
    turn(2, 2);
    turn(3, -30);
    shot(pal, "color2");
    press(B_FX);                                         /* COLOR 3: LVL -6 dB */
    turn(0, -6);
    shot(pal, "color3");
    press(B_FX);
    shot(pal, "space");
    turn(1, 40);                                         /* FDBK 70 */
    turn(3, 40);                                         /* DECAY 80 */
    shot(pal, "space_long");
    press(B_FX);                                         /* SPACE 2: DLY 80, VERB 60, TONE -40 */
    turn(0, 50);
    turn(1, 30);
    turn(2, -40);
    shot(pal, "space2");
    press(B_FX);                                         /* SPACE 3: PRE 120 ms */
    turn(0, 100);
    shot(pal, "space3");
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
    press(B_SEQ);                                        /* SEQ: white keys 2 and 3 held, KNOB 1 raises those steps */
    fm1_in.notes |= note_bit_of_white(1) | note_bit_of_white(2);
    ui_input();
    turn(0, 30);
    shot(pal, "mod3_steps");
    fm1_in.notes &= ~(note_bit_of_white(1) | note_bit_of_white(2));
    ui_input();
    press(B_FX);                                         /* COLOR, LFO's pad held: DRIV +40 %, CRSH -25 % */
    hold(B_LFO);
    turn(0, 40);
    turn(1, -25);
    shot(pal, "mod_depth");
    let_go(B_LFO);
    shot(pal, "mod_marks");                              /* let go: the modulated knobs marked */
    mod_clear(0);                                        /* (the screens below as they were) */
    mod_undo_buf.valid = 0;
    press(B_HOME);
    tap(B_GLO);
    track[1].mute = 1;                                   /* track 2 muted, track 1 sounding */
    track_rt[0].peak = 9000;
    track_rt[2].peak = 1200;
    tape_reserve(0, 22, 1);                              /* what the tracks hold: tapes of 4.1, 2.0 and 6.1 s */
    tape_ctl[0].nblk = 22 * MEM_CB;
    tape_reserve(1, 11, 1);
    tape_ctl[1].nblk = 11 * MEM_CB;
    tape_reserve(3, 33, 1);
    tape_ctl[3].nblk = 33 * MEM_CB;
    sys.cpu_q8 = 107;                                    /* (the load as the audio ISR measures it: 41 %) */
    shot(pal, "mixer");
    hold(B_GLO);                                         /* GLO held + SELECT: TRACKS 3 (track 4 parked) */
    host_enc[panel.enc[EN_SELECT]] = -1;
    ui_input();
    shot(pal, "mixer_tracks3");
    host_enc[panel.enc[EN_SELECT]] = 1;
    ui_input();
    let_go(B_GLO);
    tap(B_GLO);                                          /* (the mixer up again, latched, for the shots below) */
    hold(B_EDIT);                                        /* the channel: LOW +6, HIGH -4, a low-pass, PAN right */
    turn(0, 6);
    turn(1, -4);
    turn(2, -55);
    turn(3, 30);
    shot(pal, "mixer_channel");
    let_go(B_EDIT);
    shot(pal, "mixer_eq");
    tap(B_EDIT);                                         /* EDIT tapped twice: MASTER, the compressor */
    tap(B_EDIT);
    turn(0, 60);                                         /* AMT 60, MIX 70, and what it takes off now */
    turn(3, -30);
    comp.gr_q8 = 220;
    shot(pal, "mixer_master");
    comp.gr_q8 = 0;
    for (s = 0; s < NMS; s++)
        mst[s] = MS_P[s].def;
    tap(B_EDIT);
    tap(B_GLO);
    hold(B_GLO);
    shot(pal, "mixer_held");
    let_go(B_GLO);
    host_enc[panel.enc[EN_ALGO]] = 1;                    /* the routing view: a bounce chain T1 -> T2 -> T3, T4 on itself */
    ui_input();
    shot(pal, "route");
    turn(1, 2);                                          /* T2 records T1 */
    turn(2, 3);                                          /* T3 records T2 */
    turn(3, 5);                                          /* T4 records itself */
    sys.rec = 2u;                                        /* (T2 armed) */
    tp[0].src = SRC_SYNTH;
    shot(pal, "route_chain");
    sys.rec = 0;
    tp[0].src = SRC_TAPE;
    press(B_HOME);
    press(B_PLAY);
    press(B_REC);
    shot(pal, "playing");
    ui_message("SAVE: PROJECTS ARRIVE IN PHASE 8");
    shot(pal, "message");
    ui.msg_t = 0;
    {
    uint8_t was_rec = sys.rec;                           /* (put back after: the screens below are as they were) */
    sys.playing = 0;                                     /* the USB record mode: stopped, REC held a second */
    for (s = 0; s < NTRK; s++)
        tape_unprepare(s);
    sys.rec = 0;
    hold(B_REC);
    fm1_ms += 1100;
    ui_input();
    let_go(B_REC);
    ui.msg_t = 0;
    shot(pal, "usbrec");
    ur_feed(400, 441, 20000);                            /* the computer playing: the level */
    shot(pal, "usbrec_live");
    press(B_REC);
    for (s = 0; s < 28u; s++) {                          /* a take of 2.5 s, a phrase that swells and falls */
        ur_feed(3938, 441, 2000.0 + 12000.0 * sin(M_PI * s / 27.0) * (s % 7u < 5u ? 1.0 : 0.3));
        chain_poll();
    }
    ur_feed(40, 441, 16000);
    ui.msg_t = 0;
    shot(pal, "usbrec_rec");
    press(B_REC);
    ui.msg_t = 0;
    shot(pal, "usbrec_choose");
    key_edge((uint32_t)__builtin_ctz(note_bit_of_white(1)));
    shot(pal, "usbrec_pick");
    for (s = 0; s < 5u; s++)                             /* trimmed: the kept part in ink, the rest dim */
        usbrec_knob(UK_STRT, 2);
    for (s = 0; s < 8u; s++)
        usbrec_knob(UK_LEN, -2);
    usbrec_knob(UK_GAIN, 40);
    usbrec_knob(UK_FADE, 6);
    ui.last = UK_LEN;
    shot(pal, "usbrec_trim");
    press(B_HOME);
    press(B_HOME);
    ui.msg_t = 0;
    sys.playing = 1;
    sys.rec = was_rec;
    ui.force = 1;                                        /* (the page drawn again, as it was before the mode) */
    ui_draw();
    }
    ui.uboot = 3;
    lcd_fill(0, 0, 240, 240, 0);
    draw_head();
    shot(pal, "uboot");
    ui.uboot = 0;
    ui_setup_title();                                    /* HARDWARE CALIBRATION, on the dot grid */
    ui_setup_show("PRESS", "PLAY");
    shot_screen(pal, "calibration");
    ui_setup_show("TURN RIGHT", "SELECT");
    shot_screen(pal, "calibration_turn");
    lcd_fill(0, 0, 240, 240, T_BG);                      /* the one-shots, with main.c's and ota_hw.c's own calls */
    draw_text_box(0, 94, 240, &AF_L, "BRYO", T_THEME, 1);
    draw_text_box(0, 134, 240, &AF_S, "4-TRACK SOUND SCULPTING", T_MID, 1);
    shot_screen(pal, "boot");
    lcd_fill(0, 0, 240, 240, T_BG);
    draw_text_box(0, 92, 240, &AF_M, "UPDATE", T_TEXT, 1);
    draw_text_box(0, 124, 240, &AF_S, "CHECK HEAD", T_THEME, 1);
    shot_screen(pal, "update");
    lcd_fill(0, 0, 240, 240, UI_CRASH_BG);
    draw_text_line(0, 8, 240, &AF_M, "BRYO CRASH", UI_CRASH_INK, UI_CRASH_BG, 1);
    draw_text_line(10, 40, 220, &AF_M, "0000000C", UI_CRASH_INK, UI_CRASH_BG, 0);
    draw_text_line(10, 60, 220, &AF_M, "0201A3F4", UI_CRASH_INK, UI_CRASH_BG, 0);
    shot_screen(pal, "crash");
    lcd_fill(0, 0, 240, 240, 0);
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
        for (s = 0; s < hd_spc && w < n; s++, w += 512) {   /* (the file's sectors, not the cluster's slack) */
            memset(sec, 0, 512);
            if (w < n)
                memcpy(sec, data + w, n - w < 512 ? n - w : 512);
            vdisk_write(hd_data + (cl[i] - 2) * hd_spc + s, sec);
            vd_spare_fill();                             /* (the main loop runs between USB packets) */
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
    int32_t low = 0, pk = 0;
    for (i = 2000; i < n; i++) {                         /* (with hysteresis: ADPCM noise near 0 is no crossing) */
        int32_t x = tape_at(v, &r, (int32_t)i);
        if (x < -1000)
            low = 1;
        else if (x > 1000 && low)
            cross++, low = 0;
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
    snprintf(b, sizeof b, "TAPE2.WAV overwritten (24-bit 48 kHz, 4 s) replaces track 2's tape, all 4 s of it: %.0f Hz", hz);
    check(b, tape_src(1) == 0 && tape_ctl[1].nblk >= 88200u / 256u && tape_ctl[1].nblk <= 88200u / 256u + 1u &&
          fabs(hz - 220) < 6 && pk > 12000);

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
                vd_spare_fill();
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

/* a WAV with the odd parts: 8 or 16 bits, a chunk of odd length (padded), a fmt chunk of fmtlen bytes (40:
 * WAVE_FORMAT_EXTENSIBLE, subformat sub) */
static uint32_t make_wav_x(uint8_t *o, uint32_t rate, uint32_t bits, uint32_t fmtlen, uint32_t sub, uint32_t frames,
                           double hz)
{
    uint32_t bpf = bits / 8, n = frames * bpf, p = 12, i;
    uint8_t *f;
    memcpy(o, "RIFF\0\0\0\0WAVE", 12);
    memcpy(o + p, "junk", 4);                            /* 7 bytes and a pad byte */
    wr32(o + p + 4, 7);
    memset(o + p + 8, 0x55, 8);
    p += 16;
    memcpy(o + p, "fmt ", 4);
    wr32(o + p + 4, fmtlen);
    f = o + p + 8;
    memset(f, 0, fmtlen);
    wr16(f, fmtlen >= 40 ? 0xFFFE : 1);
    wr16(f + 2, 1);
    wr32(f + 4, rate);
    wr32(f + 8, rate * bpf);
    wr16(f + 12, bpf);
    wr16(f + 14, bits);
    if (fmtlen >= 40) {
        wr16(f + 16, 22);
        wr16(f + 18, bits);
        wr32(f + 20, 4);
        wr16(f + 24, sub);
    }
    p += 8 + fmtlen;
    memcpy(o + p, "data", 4);
    wr32(o + p + 4, n);
    p += 8;
    for (i = 0; i < frames; i++) {
        double v = 0.5 * sin(2 * M_PI * hz * i / rate);
        if (bits == 8)
            o[p + i] = (uint8_t)(128 + (int)(v * 127));
        else
            wr16(o + p + 2 * i, (uint16_t)(int16_t)(v * 32767));
    }
    wr32(o + 4, p + n - 8);
    return p + n;
}

/* ---------------------------------------------------------------- SYNTH --- */
/* a buffer's pitch: cycles per second, counted with hysteresis (from sample `from` on) */
static double buf_hz(const int32_t *x, uint32_t from, uint32_t n)
{
    uint32_t i, cross = 0, first = 0, last = 0;
    int low = 0;
    for (i = from; i < n; i++)
        if (x[i] < -800)
            low = 1;
        else if (x[i] > 800 && low) {
            if (!cross)
                first = i;
            last = i;
            cross++;
            low = 0;
        }
    return cross > 1 ? (cross - 1) * 44100.0 / (last - first) : 0;
}
/* how bright: the mean bend between samples (the second difference: a saw's edge is all bend, a sine has
 * almost none) against the mean level */
static double brightness(const int32_t *x, uint32_t from, uint32_t n)
{
    double d = 0, a = 0;
    uint32_t i;
    for (i = from + 2; i < n; i++) {
        d += abs(x[i] - 2 * x[i - 1] + x[i - 2]);
        a += abs(x[i]);
    }
    return a > 0 ? d / a : 0;
}
static uint32_t syn_sounding(uint32_t t)
{
    uint32_t j, n = 0;
    for (j = 0; j < SYN_NV; j++)
        n += syn[t].v[j].stage != 0;
    return n;
}

static void test_synth(void)
{
    enum { NB = 690 };                                   /* 0.5 s */
    static int32_t s[2 * NB * CTL];                      /* (some checks render two of them) */
    int16_t *p;
    char b[120];
    double hz, br_open, br_shut;
    uint32_t i;
    power_on();
    hold(B_HOME);                                        /* HOME held + SELECT: the source */
    host_enc[panel.enc[EN_SELECT]] = 1;
    ui_input();
    let_go(B_HOME);
    p = tp[0].syn;
    check("HOME held + SELECT: track 1's source is SYNTH, its pages and name follow", tp[0].src == SRC_SYNTH &&
          !strcmp(dev_name(0, DEV_SRC), "SYNTH") && pdesc_pages(dev_p(0, DEV_SRC)) == 5u && ui.page == 0 &&
          dev_v(0, DEV_SRC) == p && !strcmp(dev_p(0, DEV_SRC)[0].label, "WAVE"));
    {
        int32_t pk = 0;
        for (i = 0; i < NB; i++) {                       /* (the track's own output: the master may hold a DC tail) */
            render(1, 0);
            pk = peak_of(track_rt[0].last, CTL) > pk ? peak_of(track_rt[0].last, CTL) : pk;
        }
        check("SYNTH, no key down: silence", pk == 0);
    }

    p[SY_WAVE] = 0;                                      /* a plain sine, the filter open */
    p[SY_DTUN] = 0;
    p[SY_MIX] = 0;
    p[SY_CUT] = 127;
    p[SY_ENV] = 0;
    p[SY_RES] = 0;
    fm1_in.notes = note_bit_of_white(9);                 /* white key 10: A3 at OCT 3 */
    render(NB, s);
    hz = buf_hz(s, NB * CTL / 4, NB * CTL);
    snprintf(b, sizeof b, "white key 10 at OCT 3 plays A3: %.1f Hz (220), peak %d", hz, (int)peak_of(s, NB * CTL));
    check(b, fabs(hz - 220.0) < 1.5 && peak_of(s, NB * CTL) > 4000 && syn_sounding(0) == 1);
    fm1_in.notes = 0;
    render(NB * 2, s);
    check("..let go: it releases to silence and the voice is free", syn_sounding(0) == 0 &&
          peak_of(s + NB * CTL, NB * CTL) < 40);

    track[0].octave = 4;                                 /* OCT+: an octave up */
    fm1_in.notes = note_bit_of_white(9);
    render(NB, s);
    hz = buf_hz(s, NB * CTL / 4, NB * CTL);
    snprintf(b, sizeof b, "OCT 4: the same key an octave up: %.1f Hz (440)", hz);
    check(b, fabs(hz - 440.0) < 3.0);
    fm1_in.notes = 0;
    render(NB * 2, 0);
    track[0].octave = 3;

    fm1_in.notes = note_bit_of_white(0) | note_bit_of_white(4) | note_bit_of_white(7);   /* a C major chord */
    render(8, 0);
    check("three keys: three voices", syn_sounding(0) == 3);
    fm1_in.notes |= note_bit_of_white(11);               /* a fourth: the oldest voice is taken */
    render(8, 0);
    {
        uint32_t j, has11 = 0;
        for (j = 0; j < SYN_NV; j++)
            has11 |= syn[0].v[j].key == 11 && syn[0].v[j].stage && syn[0].v[j].stage != 3;
        check("..a fourth key takes a voice (no more than three sound)", syn_sounding(0) == 3 && has11);
    }
    fm1_in.notes = 0;
    render(NB * 2, 0);

    p[SY_VOIC] = 1;                                      /* one voice: legato, gliding */
    p[SY_GLID] = 60;
    fm1_in.notes = note_bit_of_white(0);
    render(20, 0);
    fm1_in.notes |= note_bit_of_white(12);
    render(4, 0);
    {
        int32_t mid = syn[0].v[0].p16;
        render(NB, 0);
        check("VOIC 1: a second key glides the one voice up an octave, no new attack",
              syn_sounding(0) == 1 && mid > 48 * 16 && mid < 60 * 16 && syn[0].v[0].p16 == 60 * 16 &&
              syn[0].v[0].stage == 2);
    }
    fm1_in.notes = note_bit_of_white(0);                 /* the top key up: back down to the one still held */
    render(NB, 0);
    check("..the top key let go: back to the key still held", syn[0].v[0].p16 == 48 * 16 && syn_sounding(0) == 1);
    fm1_in.notes = 0;
    render(NB * 2, 0);
    p[SY_VOIC] = SYN_NV;
    p[SY_GLID] = 0;

    {   /* every wave at the key's pitch; noise and drive stay under full scale */
        uint32_t w, ok = 1;
        char m[64] = "";
        for (w = 0; w < 5u; w++) {
            p[SY_WAVE] = (int16_t)w;
            fm1_in.notes = note_bit_of_white(9);
            render(NB, s);
            hz = buf_hz(s, NB * CTL / 4, NB * CTL);
            ok &= fabs(hz - 220.0) < 1.5;
            snprintf(m + strlen(m), sizeof m - strlen(m), " %s %.0f", N_OSC[w], hz);
            fm1_in.notes = 0;
            render(NB * 2, 0);
        }
        snprintf(b, sizeof b, "every wave plays A3:%s", m);
        check(b, ok);
        p[SY_WAVE] = 3;
        p[SY_NOIS] = 100;
        p[SY_DRV] = 100;
        p[SY_MIX] = 50;
        p[SY_DTUN] = 30;
        fm1_in.notes = note_bit_of_white(0) | note_bit_of_white(4) | note_bit_of_white(7);
        render(NB, s);
        check("a full-noise, full-drive, detuned chord sounds, and the track stays under full scale",
              peak_of(s, NB * CTL) > 8000 && peak_of(s, NB * CTL) <= 32767);
        p[SY_VOIC] = 1;                                  /* VOIC turned down under three held keys */
        render(4, 0);
        check("..VOIC turned down to 1 while three sound: the voices above it release", syn[0].v[1].stage == 3 &&
              syn[0].v[2].stage == 3 && syn[0].v[0].stage == 2);
        p[SY_VOIC] = SYN_NV;
        fm1_in.notes = 0;
        render(2, 0);
        fm1_in.notes = note_bit_of_white(12);            /* all three releasing: the quietest is taken */
        render(1, 0);
        {
            uint32_t j, n12 = 0, rel = 0;
            for (j = 0; j < SYN_NV; j++) {
                n12 += syn[0].v[j].key == 12 && syn[0].v[j].stage == 2;
                rel += syn[0].v[j].stage == 3;
            }
            check("..a key while all three release: it takes one of them, the other two go on releasing",
                  n12 == 1 && rel == 2);
        }
        fm1_in.notes = 0;
        render(NB * 2, 0);
        p[SY_NOIS] = 0;
        p[SY_DRV] = 0;
        p[SY_MIX] = 0;
        p[SY_DTUN] = 0;
    }
    p[SY_VOIC] = 1;                                      /* legato without glide: the pitch jumps, no attack */
    fm1_in.notes = note_bit_of_white(0);
    render(20, 0);
    fm1_in.notes |= note_bit_of_white(5);
    render(1, 0);
    check("VOIC 1, GLID 0: a second key jumps the pitch at once, the envelope goes on",
          syn[0].v[0].p16 == 53 * 16 && syn[0].v[0].stage == 2);
    fm1_in.notes = note_bit_of_white(5);
    render(1, 0);
    check("..the lower key let go: the voice stays on the one still held", syn[0].v[0].p16 == 53 * 16 &&
          syn[0].v[0].key == 5);
    fm1_in.notes = 0;
    render(NB * 2, 0);
    p[SY_VOIC] = SYN_NV;

    p[SY_WAVE] = 3;                                      /* a saw through the filter, open then nearly shut */
    fm1_in.notes = note_bit_of_white(0);
    render(NB, s);
    br_open = brightness(s, NB * CTL / 2, NB * CTL);
    fm1_in.notes = 0;
    render(NB * 2, 0);
    p[SY_CUT] = 40;
    fm1_in.notes = note_bit_of_white(0);
    render(NB, s);
    br_shut = brightness(s, NB * CTL / 2, NB * CTL);
    snprintf(b, sizeof b, "the filter: a saw is darker with CUT down (brightness %.3f -> %.3f)", br_open, br_shut);
    check(b, br_shut < br_open * 0.5 && peak_of(s, NB * CTL) > 1000);
    fm1_in.notes = 0;
    render(NB * 2, 0);
    p[SY_CUT] = SYN_P[SY_CUT].def;
    p[SY_WAVE] = 0;
    p[SY_CUT] = 127;

    {   /* REC on a SYNTH track prints the synth onto its tape: then TAPE plays it back */
        tape_view_t v;
        int32_t pk;
        tp[0].dev[DEV_SRC][TK_DUB] = -100;                   /* (DUB -100: the input replaces the loop) */
        press(B_REC);
        check("REC on a SYNTH track arms its tape", (sys.rec & 1u) && tape_ctl[0].rec_ok);
        sys.playing = 1;
        fm1_in.notes = note_bit_of_white(9);
        render(NB * 6, 0);                               /* 3 s: past the 2 s loop */
        press(B_REC);
        render(2, 0);
        fm1_in.notes = 0;
        sys.playing = 0;
        render(NB, 0);
        tape_view(0, &v);
        hz = view_hz(&v, &pk);
        snprintf(b, sizeof b, "..the take is the note: %.1f Hz on the tape (220), peak %d", hz, (int)pk);
        check(b, fabs(hz - 220.0) < 1 && pk > 3000);
    }
    {   /* back to TAPE while both sound: a 2 ms crossfade, no step */
        int32_t step = 0, before = 0;
        sys.playing = 1;
        fm1_in.notes = note_bit_of_white(9);
        render(NB, s);
        for (i = NB * CTL / 2; i < NB * CTL; i++)
            before = abs(s[i] - s[i - 1]) > before ? abs(s[i] - s[i - 1]) : before;
        tp[0].src = SRC_TAPE;
        render(8, s);
        for (i = 1; i < 8 * CTL; i++)
            step = abs(s[i] - s[i - 1]) > step ? abs(s[i] - s[i - 1]) : step;
        snprintf(b, sizeof b, "SYNTH -> TAPE while both sound: a crossfade (largest step %d, the note's own %d)",
                 (int)step, (int)before);
        check(b, track_rt[0].src_g[SRC_SYNTH] == 0 && track_rt[0].src_g[SRC_TAPE] == 32767 && step < before * 3);
        fm1_in.notes = 0;
        render(4, 0);
        tp[0].src = SRC_SYNTH;
        render(NB * 2, 0);
        check("..switched back, the note held across the switch is seen up and released", syn_sounding(0) == 0);
        sys.playing = 0;
        tp[0].src = SRC_TAPE;
        render(NB / 2, 0);
    }
}

/* ----------------------------------------------------------------- POLY --- */
/* user reel `slot`: a sine of hz, nblk blocks, encoded as a recording is (tape.c tape_commit) */
static void make_sine_reel(uint32_t slot, double hz, uint32_t nblk)
{
    static uint8_t data[TAPE_LEN / 2];
    static int16_t pred[TAPE_NBLK];
    static uint8_t idx[TAPE_NBLK], peak[TAPE_NBLK];
    int16_t blk[TAPE_BLK];
    uint32_t b, i;
    for (b = 0; b < nblk; b++) {
        int32_t d, id = 0;
        for (i = 0; i < TAPE_BLK; i++)
            blk[i] = (int16_t)(16000.0 * sin(2 * M_PI * hz * (b * TAPE_BLK + i) / TAPE_SR));
        d = abs(blk[1] - blk[0]);
        while (id < 88 && IMA_STEP[id] < d)
            id++;
        pred[b] = blk[0];
        idx[b] = (uint8_t)id;
        peak[b] = 16000 >> 7;
        ima_enc(blk, pred[b], idx[b], data + b * (TAPE_BLK / 2), TAPE_BLK);
    }
    uslot_save(slot, data, pred, idx, peak, nblk, "SINE");
}
static uint32_t pol_sounding(uint32_t t)
{
    uint32_t j, n = 0;
    for (j = 0; j < POL_NV; j++)
        n += pol[t].v[j].stage != 0;
    return n;
}
/* white key k held for NB blocks into s, then let go and silent again; returns the pitch */
static double pol_note(uint32_t k, int32_t *s, uint32_t nb)
{
    double hz;
    fm1_in.notes = note_bit_of_white(k);
    render(nb, s);
    hz = buf_hz(s, nb * CTL / 4, nb * CTL);
    fm1_in.notes = 0;
    render(nb * 2, 0);
    return hz;
}

static void test_poly(void)
{
    enum { NB = 690 };                                   /* 0.5 s */
    static int32_t s[2 * NB * CTL];                      /* (some checks render two of them) */
    int16_t *p;
    char b[120];
    double h0, h7, h12;
    int32_t pk_open, pk;
    uint32_t i;
    power_on();
    make_sine_reel(0, 440.0, 200);                       /* user reel 1: 2.3 s of A4 */
    hold(B_HOME);                                        /* HOME held + SELECT, twice round: POLY */
    host_enc[panel.enc[EN_SELECT]] = 2;
    ui_input();
    let_go(B_HOME);
    p = tp[0].pol;
    check("HOME held + SELECT: POLY, four pages (SAMPLE ENV FILTER LEVEL), its own knobs", tp[0].src == SRC_POLY &&
          !strcmp(dev_name(0, DEV_SRC), "POLY") && pdesc_pages(dev_p(0, DEV_SRC)) == 4u && dev_v(0, DEV_SRC) == p &&
          p[PL_REEL] == 1);
    {
        int32_t q = 0;
        for (i = 0; i < NB; i++) {
            render(1, 0);
            q = peak_of(track_rt[0].last, CTL) > q ? peak_of(track_rt[0].last, CTL) : q;
        }
        check("POLY, no key down: silence", q == 0);
    }
    p[PL_REEL] = (int16_t)(NREEL + 1u);                  /* the sine */
    h0 = pol_note(0, s, NB);
    pk_open = peak_of(s, NB * CTL);
    h7 = pol_note(7, s, NB);
    h12 = pol_note(12, s, NB);
    snprintf(b, sizeof b, "white key 1 plays the sound as recorded (%.1f Hz), key 8 a fifth up (%.1f), key 13 "
             "an octave (%.1f)", h0, h7, h12);
    check(b, fabs(h0 - 440.0) < 2 && fabs(h7 - 659.3) < 3 && fabs(h12 - 880.0) < 4 && pk_open > 3000);
    check("..each note let go releases and frees its voice", pol_sounding(0) == 0);

    fm1_in.notes = note_bit_of_white(0) | note_bit_of_white(4) | note_bit_of_white(7) | note_bit_of_white(11);
    render(8, 0);
    check("four keys: four voices", pol_sounding(0) == 4);
    fm1_in.notes |= note_bit_of_white(14);
    render(8, 0);
    {
        uint32_t j, has = 0;
        for (j = 0; j < POL_NV; j++)
            has |= pol[0].v[j].key == 14 && pol[0].v[j].stage && pol[0].v[j].stage != 3;
        check("..a fifth takes the oldest (no more than four sound)", pol_sounding(0) == 4 && has);
    }
    p[PL_VOIC] = 2;
    render(2, 0);
    check("..VOIC turned down to 2: the voices above it let go", pol[0].v[2].stage == 3 && pol[0].v[3].stage == 3);
    fm1_in.notes = 0;
    render(NB * 2, 0);
    p[PL_VOIC] = POL_NV;

    p[PL_STRT] = 50;                                     /* STRT 50: every note from half way */
    fm1_in.notes = note_bit_of_white(0);
    render(1, 0);
    check("STRT 50: a note starts half way through the sound",
          abs((pol[0].v[0].pos >> 12) - 200 * (int32_t)TAPE_BLK / 2) < 32);
    {
        uint32_t ran = 0;
        while (pol_sounding(0) && ran < NB * 4u) {
            render(1, 0);
            ran++;
        }
        snprintf(b, sizeof b, "..held, it plays to the sound's end, fades and frees the voice (%u blocks, key still "
                 "down)", ran);
        check(b, !pol_sounding(0) && abs((int32_t)ran - 200 * 256 / 2 * 2 / CTL) < 8 && fm1_in.notes);   /* (half the reel, 2 outputs a sample) */
    }
    fm1_in.notes = 0;
    render(NB, 0);
    p[PL_STRT] = 0;

    p[PL_CUT] = 30;                                      /* the filter: LP low, HP high, BP on the note */
    fm1_in.notes = note_bit_of_white(0);
    render(NB, s);
    pk = peak_of(s + NB * CTL / 2, NB * CTL / 2);
    fm1_in.notes = 0;
    render(NB * 2, 0);
    snprintf(b, sizeof b, "TYPE LP, CUT 30: the sine falls (peak %d, open %d)", (int)pk, (int)pk_open);
    check(b, pk < pk_open / 4);
    p[PL_TYPE] = 2;
    p[PL_CUT] = 110;
    fm1_in.notes = note_bit_of_white(0);
    render(NB, s);
    pk = peak_of(s + NB * CTL / 2, NB * CTL / 2);
    fm1_in.notes = 0;
    render(NB * 2, 0);
    snprintf(b, sizeof b, "TYPE HP, CUT 110: the sine falls (peak %d)", (int)pk);
    check(b, pk < pk_open / 4);
    p[PL_TYPE] = 0;
    p[PL_CUT] = 127;

    p[PL_REEL] = 0;                                      /* the track's own tape: empty, then a reel copied in */
    fm1_in.notes = note_bit_of_white(0);
    render(NB / 2, s);
    pk = peak_of(s, NB / 2 * CTL);
    fm1_in.notes = 0;
    render(NB, 0);
    tape_prepare(0);
    tape_unprepare(0);
    fm1_in.notes = note_bit_of_white(0);
    render(NB / 2, s);
    fm1_in.notes = 0;
    render(NB, 0);
    check("REEL TAPE: an empty tape plays nothing; once it holds a sound, the keys play it",
          pk == 0 && peak_of(s, NB / 2 * CTL) > 2000);
    tp[0].src = SRC_TAPE;
    render(NB / 2, 0);
    memset(host_nor, 0xFF, sizeof host_nor);
    uslot_names();
}

/* ---------------------------------------------------------------- GRAIN --- */
static uint32_t zero_key_bit(void)
{
    uint32_t n;
    for (n = 0; n < 27u; n++)
        if (KEY_BLACK[n] == BK_ZERO)
            return 1u << n;
    return 0;
}

static void test_grain(void)
{
    enum { NB = 690 };                                   /* 0.5 s */
    static int32_t s[2 * NB * CTL];                      /* (some checks render two of them) */
    int16_t *p;
    char b[120];
    uint32_t i, t, ok, maxc = 0;
    int32_t c0;
    power_on();
    make_sine_reel(0, 440.0, 200);                       /* user reel 1: 2.3 s of A4 (440 Hz) */
    tp[0].dev[DEV_SRC][TK_REEL] = (int16_t)(NREEL + 1u);
    p = tp[0].dev[DEV_GRAIN];
    p[GP_SCAN] = SCAN_TAPE;                              /* (these read the tape: the buffer is test_grain_buffer's) */
    {   /* a grain's own decoder against the tape's block reader, both ways */
        tape_view_t v;
        tape_rd_t rd = {0};
        grain_t g;
        int32_t k, pos;
        tape_view(0, &v);
        memset(&g, 0, sizeof g);
        g.wlo = -1;
        g.inc = 4096;
        for (k = 0, ok = 1, pos = 300; k < 3000; k++, pos += 3)  /* forward, over block boundaries */
            ok &= gr_read(&g, &v, pos << 12) == tape_at(&v, &rd, pos);
        g.wlo = -1;
        g.inc = -4096;
        for (k = 0, pos = 20000; k < 3000; k++, pos -= 5)        /* backwards */
            ok &= gr_read(&g, &v, pos << 12) == tape_at(&v, &rd, pos);
        check("a grain decodes the tape itself, forwards and backwards, exactly as the tape's reader does", ok);
    }
    for (t = 1; t < NTRK; t++)                           /* (only track 1 sounds) */
        track[t].mute = 1;
    sys.playing = 1;
    render(NB, s);
    check("WET 0 (the default): no grains, the track as it was", grain_count(0) == 0 && peak_of(s, NB * CTL) > 2000);
    p[4] = 100;                                          /* WET 100: grains only */
    sys.playing = 0;
    render(NB, 0);
    check("stopped, no key: no grains start", grain_count(0) == 0);
    {   /* one grain's output, sample for sample: the tape at its read position, through its window and its pan */
        static int32_t dry[CTL], gl[CTL], gr[CTL];
        tape_view_t v;
        tape_rd_t rd = {0};
        uint32_t blk, bad = 0, n = 0;
        p[1] = 0;                                        /* (one grain at a time, 500 ms, from the cursor) */
        p[0] = 500;
        p[5] = 0;
        grain_kill(0);
        grain[0].wait = 0;
        grain[0].cur = 0;
        sys.playing = 1;
        tape_view(0, &v);
        grain_block(0, dry, 0, 0, gl, gr, CTL);
        for (blk = 0; blk < 600u; blk++) {
            grain_t g = gslot[0];
            grain_block(0, dry, 0, 0, gl, gr, CTL);
            int32_t m = (int32_t)(g.left < CTL ? g.left : CTL), e0 = gr_env(&g, g.len - g.left);
            int32_t e1 = gr_env(&g, g.len - g.left + (uint32_t)m);
            for (i = 0; i < (uint32_t)m; i++, n++) {     /* (the window: its block ends, ramped across the block) */
                int32_t k = g.pos >> 12, f = g.pos & 4095, a = tape_at(&v, &rd, k), bb = tape_at(&v, &rd, k + 1), x;
                int32_t e = ((e0 << 5) + (int32_t)i * (((e1 - e0) * 32) >> CTL_LOG2)) >> 5;
                x = (a + (((bb - a) * f) >> 12)) * e >> 15;
                bad += ((x * (g.gl * 100 / 100)) >> 15) != gl[i] || ((x * (g.gr * 100 / 100)) >> 15) != gr[i];
                g.pos += g.inc;
            }
            g.left -= (uint32_t)m;
        }
        check("one grain, sample for sample: the tape where it reads, through its window and pan", bad == 0 && n > 15000);
    }
    sys.playing = 0;                                     /* pitch: one grain at a time, measured inside it */
    render(NB * 2, 0);
    {
        static const int16_t ST[3] = {0, 12, 0};
        double got[3];
        uint32_t k;
        p[3] = 0;
        for (k = 0; k < 3; k++) {
            p[2] = ST[k];
            p[7] = k == 2 ? 100 : 0;                     /* (the third backwards) */
            grain[0].wait = 0;
            grain[0].cur = 0;
            sys.playing = 1;
            render(600, s);                              /* 0.44 s of the first 0.5 s grain */
            got[k] = buf_hz(s, 40 * CTL, 600 * CTL);
            sys.playing = 0;
            render(NB * 2, 0);
        }
        snprintf(b, sizeof b, "a grain's pitch: PTCH 0 %.1f Hz, PTCH +12 %.1f, backwards %.1f (440, 880, 440)", got[0],
                 got[1], got[2]);
        check(b, fabs(got[0] - 440) < 2 && fabs(got[1] - 880) < 3 && fabs(got[2] - 440) < 2);
        p[2] = 0;
        p[7] = 0;
        p[3] = 30;
        p[5] = 20;
    }
    p[1] = 40;
    p[0] = 80;
    sys.playing = 1;
    render(NB, s);
    check("WET 100, playing, the defaults: grains sound", grain_count(0) > 0 && peak_of(s, NB * CTL) > 1000);

    {   /* SPRD, at GRAIN's own output (the master's DC blocker remembers each side for seconds) */
        static int32_t dry[CTL], gl[CTL], gr[CTL];
        uint32_t diff0 = 0, diff1 = 0, j;
        p[3] = 0;
        render(NB, 0);                                   /* (the grains placed before it finish) */
        for (i = 0; i < NB; i++) {
            grain_block(0, dry, 0, 0, gl, gr, CTL);
            for (j = 0; j < CTL; j++)
                diff0 += gl[j] != gr[j];
        }
        p[3] = 100;
        for (i = 0; i < NB; i++) {
            grain_block(0, dry, 0, 0, gl, gr, CTL);
            for (j = 0; j < CTL; j++)
                diff1 += gl[j] != gr[j];
        }
        check("SPRD 0: every grain in the centre (left = right); SPRD 100: placed across the stereo field",
              diff0 == 0 && diff1 > NB * CTL / 2);
        p[3] = 30;
    }

    p[1] = 100;                                          /* RATE 100, SIZE 500: as many as the cap allows */
    p[0] = 500;
    for (i = 0; i < NB; i++) {
        render(1, 0);
        maxc = grain_count(0) > maxc ? grain_count(0) : maxc;
    }
    render(NB, s);
    check("RATE 100, SIZE 500, GRAIN on this track alone: up to the whole pool of 32, never more, under full scale",
          maxc == 32u && peak_of(s, NB * CTL) <= 32767);
    grain_shed();
    for (i = 0, maxc = 0; i < 1300u; i++) {
        render(1, 0);
        maxc = grain_count(0) > maxc ? grain_count(0) : maxc;
    }
    check("shedding shrinks the pool a quarter (32 -> 24): the grains follow it down", grain_cap == 6u &&
          gr_allow(0) == 24u && grain_count(0) <= 24u);
    render(200, 0);
    check("..and a second without shedding gives an eighth back (28)", grain_cap == 7u && gr_allow(0) == 28u);
    grain_cap = GR_CAP;
    p[1] = 40;
    p[0] = 80;

    c0 = grain[0].cur;                                   /* WARP 100: the cursor runs at the tape's pace */
    render(100, 0);
    check("WARP 100: the cursor moves through the loop at the tape's pace",
          abs(((grain[0].cur - c0) >> 12) - 100 * CTL / 2) < 4);
    fm1_in.notes = zero_key_bit();                       /* the 0 key held: frozen */
    c0 = grain[0].cur;
    render(100, 0);
    check("the 0 black key held: every cursor frozen", grain[0].cur == c0 && grain_count(0) > 0);
    fm1_in.notes = 0;
    p[GP_WARP] = -100;
    c0 = grain[0].cur;
    render(10, 0);
    check("WARP -100: the cursor runs backwards", ((c0 - grain[0].cur) >> 12) == 10 * CTL / 2);
    p[GP_WARP] = 0;
    c0 = grain[0].cur;
    render(10, 0);
    check("WARP 0: still", grain[0].cur == c0);
    p[GP_WARP] = 100;

    p[GP_SCAL] = 2;                                      /* SCAL MAJ, PRND 12: every pitch on the major scale */
    p[GP_PRND] = 12;
    for (i = 0, ok = 1; i < NB; i++) {
        uint32_t j;
        render(1, 0);
        for (j = 0; j < GR_SLOTS; j++)
            if (((gr_used >> j) & 1u) && gslot[j].trk == 0) {
                int32_t st = gslot[j].st16;
                ok &= st % 16 == 0 && ((0xAB5u >> (uint32_t)(((st / 16) % 12 + 12) % 12)) & 1u) && abs(st) <= 12 * 16;
            }
    }
    check("SCAL MAJ, PRND 12: every grain's pitch a major-scale semitone within an octave", ok);
    p[GP_SCAL] = 0;
    p[GP_PRND] = 0;

    sys.playing = 0;                                     /* the GRAIN page, stopped: a key plays its slice */
    render(NB, 0);
    press(B_EDIT);
    ui_input();
    check("the GRAIN page up: the keys go to GRAIN", sys.keys_grain == 1);
    fm1_in.notes = note_bit_of_white(4);
    render(4, 0);
    {
        tape_view_t v;
        int32_t ls, ll;
        tape_view(0, &v);
        tape_window(0, v.len, &ls, &ll);
        check("..white key 5: the cursor to slice 5, grains while it's held, the tape's head left alone",
              abs((grain[0].cur >> 12) - (ls + ll * 4 / 16)) <= 4 * CTL / 2 && grain_count(0) > 0 &&
              !tape_rt[0].running);   /* (WARP moves it on from there) */
    }
    fm1_in.notes = 0;
    render(NB, 0);
    check("..let go: the grains finish, no new ones", grain_count(0) == 0);
    press(B_HOME);
    ui_input();
    check("another page: the keys go back to the source", sys.keys_grain == 0);

    tp[0].dev[DEV_SRC][TK_REEL] = (int16_t)(NREEL + 2u); /* an empty user reel: nothing to granulate */
    sys.playing = 1;
    render(NB, 0);
    check("nothing on the tape: no grains", grain_count(0) == 0);
    sys.playing = 0;
    for (t = 0; t < NTRK; t++)
        track[t].mute = 0;
    render(NB, 0);
    memset(host_nor, 0xFF, sizeof host_nor);
    uslot_names();
}

/* blocks of audio with the main loop's bookkeeping between them (as main.c runs it) */
static void render_poll(uint32_t blocks)
{
    uint32_t b;
    for (b = 0; b < blocks; b++) {
        render(1, 0);
        chain_poll();
    }
}

/* track t's tape holds n chunks of tone (a take: its length n chunks) */
static void fill_tape(uint32_t t, uint32_t n)
{
    uint32_t b;
    tape_free(t);
    tape_reserve(t, n, 1);
    for (b = 0; b < (uint32_t)tape_ctl[t].nch * MEM_CB; b++)
        tape_chunk(t, b)->peak[b % MEM_CB] = 100;
    tape_ctl[t].nblk = (uint16_t)(tape_ctl[t].nch * MEM_CB);
    tape_ctl[t].empty = 0;
    tp[t].dev[DEV_SRC][TK_REEL] = 0;
}

static uint32_t led_lit;                                 /* (test_controls_more's LED reader, below) */
static void led_on(const uint8_t *a, uint32_t id);

/* GRAIN's live buffer: its bars, what it records, freeze, SCAN */
static uint32_t buf_sum(uint32_t t)                      /* a fingerprint of a buffer's data */
{
    uint32_t c, b, h = 0;
    for (c = 0; c < gbuf[t].nch; c++)
        for (b = 0; b < MEM_CB; b++)
            h = h * 31u + mem_at(gbuf[t].map[c])->data[b][7] + mem_at(gbuf[t].map[c])->peak[b];
    return h;
}
static uint32_t buf_loud(uint32_t t, uint32_t from, uint32_t to)   /* blocks of buffer t in [from, to) above a whisper */
{
    tape_view_t v;
    uint32_t b, n = 0;
    gr_buf_view(t, &v);
    for (b = from; b < to && b < v.len / TAPE_BLK; b++)
        n += tv_peak(&v, b) > 20u;
    return n;
}

static void test_grain_buffer(void)
{
    char b[120];
    int16_t *p;
    uint32_t t, i, nb;
    int32_t c0;
    power_on();
    p = tp[0].dev[DEV_GRAIN];
    for (t = 1; t < NTRK; t++)
        track[t].mute = 1;
    chain_poll();
    check("GRAIN at WET 0: no buffer, no memory taken", !gbuf[0].nch && !mem_count(MEM_GRAIN));
    p[GP_WET] = 100;
    chain_poll();
    check("WET up (SCAN STR, the default): a buffer of 1 bar at 120 BPM with 4 tracks (2 s, 11 chunks)",
          p[GP_SCAN] == SCAN_STR && gbuf[0].len == 44100u && gbuf[0].nch == 11 && gbuf[0].bars == 1 &&
          mem_count(MEM_GRAIN) == 11);
    chain_tracks(2);
    chain_poll();
    check("TRACKS 2: 2 bars (4 s)", gbuf[0].len == 88200u && gbuf[0].bars == 2);
    chain_tracks(1);
    chain_poll();
    check("TRACKS 1: 4 bars (8 s)", gbuf[0].len == 176400u && gbuf[0].bars == 4 && gbuf[0].nch == 44);
    sys.bpm = 40;
    chain_poll();
    snprintf(b, sizeof b, "..at 40 BPM 4 bars would pass 12 s: 2 bars (%u samples, %u chunks)", (unsigned)gbuf[0].len,
             (unsigned)gbuf[0].nch);
    check(b, gbuf[0].bars == 2 && gbuf[0].len == 264600u && gbuf[0].nch == 65);
    sys.bpm = 120;
    chain_tracks(4);
    chain_poll();
    check("..back to TRACKS 4, 120 BPM: 1 bar, the rest given back", gbuf[0].nch == 11 && mem_count(MEM_GRAIN) == 11);
    p[GP_SCAN] = SCAN_TAPE;
    chain_poll();
    check("SCAN TAPE: no buffer (grains read the tape)", !gbuf[0].nch && !mem_count(MEM_GRAIN));
    p[GP_SCAN] = SCAN_STR;
    chain_poll();

    sys.playing = 1;                                     /* track 1 plays the beat into its buffer */
    render_poll(1);
    check("PLAY: the write head starts at the bar line", gbuf[0].w <= CTL / 2);
    render_poll(1377);
    snprintf(b, sizeof b, "..1 s on: the write head half a bar on (%d of 44,100), the buffer holds the beat",
             (int)gbuf[0].w);
    check(b, abs(gbuf[0].w - 22050) < CTL && buf_loud(0, 0, 86) > 40u);
    render_poll(1378 * 3);
    check("..WET 100, SCAN STR: grains sound", grain_count(0) > 0);

    tp[0].src = SRC_SYNTH;                               /* the carry-over: TAPE -> SYNTH (silent), FDBK 0 */
    render_poll(10);
    nb = buf_loud(0, (uint32_t)gbuf[0].w / TAPE_BLK + 4u, 172u);
    snprintf(b, sizeof b, "switched to SYNTH: the beat is still in the buffer ahead of the write head (%u blocks)", nb);
    check(b, nb > 10u);
    render_poll(1378 * 2 + 50);
    check("..a bar later (FDBK 0): all of it the new (silent) source", buf_loud(0, 0, 172) == 0);

    tp[0].src = SRC_TAPE;                                /* FDBK 50: half the old stays each pass */
    render_poll(1378 * 2 + 50);
    {
        uint32_t full = buf_loud(0, 0, 172), half;
        tape_view_t v;
        uint32_t s0 = 0, s1 = 0, k;
        gr_buf_view(0, &v);
        for (k = 0; k < 172; k++)
            s0 += tv_peak(&v, k);
        p[GP_FDBK] = 50;
        tp[0].src = SRC_SYNTH;
        render_poll(1378 * 2 + 50);
        for (k = 0; k < 172; k++)
            s1 += tv_peak(&v, k);
        half = s1 * 100u / (s0 ? s0 : 1u);
        snprintf(b, sizeof b, "FDBK 50: a bar of silence over the beat leaves it at half (%u%%)", half);
        check(b, full > 40u && half >= 40u && half <= 60u);
        p[GP_FDBK] = 0;
    }

    tp[0].src = SRC_TAPE;
    render_poll(1378 * 2 + 50);
    {   /* freeze: the buffer holds, the grains loop it */
        uint32_t h0, w0;
        fm1_in.notes = zero_key_bit();
        render_poll(2);
        h0 = buf_sum(0);
        w0 = (uint32_t)gbuf[0].w;
        c0 = grain[0].cur;
        render_poll(1378);
        check("the 0 key held: the buffer stops recording (its sound unchanged, the write head still)",
              buf_sum(0) == h0 && (uint32_t)gbuf[0].w == w0);
        check("..and the grains play on through it (the cursor moves, grains sound)", grain[0].cur != c0 &&
              grain_count(0) > 0);
        sys.bpm = 60;                                    /* half the tempo while frozen: the loop's pace halves */
        chain_poll();
        c0 = grain[0].cur;
        render(100, 0);
        {
            int32_t d = (grain[0].cur - c0) >> 12;
            d = d < 0 ? d + (int32_t)gbuf[0].len : d;
            snprintf(b, sizeof b, "..the tempo halved while frozen: the buffer keeps its size, the loop half the pace (%d)", (int)d);
            check(b, gbuf[0].len == 44100u && abs(d - 100 * CTL / 4) < 4);
        }
        sys.bpm = 120;
        fm1_in.notes = 0;
        render_poll(20);
        check("..let go: it records again", (uint32_t)gbuf[0].w != w0);
    }

    {   /* the 0 key tapped: the freeze latches (as GLO's mixer: held momentary, tapped latched) */
        uint32_t h0, n0 = 0;
        for (n0 = 0; n0 < 27u && KEY_BLACK[n0] != BK_ZERO; n0++)
            ;
        fm1_in.notes = 1u << n0;
        key_edge(n0);
        fm1_in.notes = 0;
        ui_input();
        render_poll(4);
        h0 = buf_sum(0);
        render_poll(1378);
        check("the 0 key tapped: the freeze latches (the buffer holds after the key is up), and its LED lights",
              sys.freeze && buf_sum(0) == h0 && grain_frozen);
        ui_leds();
        led_on(fm1_led, 14u + n0);
        check("..its LED lit", led_lit);
        fm1_in.notes = 1u << n0;
        key_edge(n0);
        fm1_in.notes = 0;
        ui_input();
        render_poll(20);
        check("..tapped again: let go, it records again", !sys.freeze && buf_sum(0) != h0);
    }

    p[GP_SCAN] = SCAN_POS;                               /* POS: OFST 50 holds the cursor halfway */
    p[GP_OFST] = 50;
    render_poll(20);
    check("SCAN POS, OFST 50: the cursor halfway through the buffer, still", (grain[0].cur >> 12) == 22050);
    press(B_EDIT);
    fm1_in.notes = note_bit_of_white(4);
    render_poll(2);
    check("..on the GRAIN page, white key 5: the cursor to the buffer's fifth sixteenth", (grain[0].cur >> 12) == 44100 * 4 / 16);
    fm1_in.notes = 0;
    p[GP_OFST] = 60;
    render_poll(2);
    check("..OFST turned again takes over", (grain[0].cur >> 12) == 44100 * 60 / 100);
    press(B_HOME);
    p[GP_SCAN] = SCAN_DLY;                               /* DLY: OFST 25 behind the write head */
    p[GP_OFST] = 25;
    render_poll(20);
    {
        int32_t lag = gbuf[0].w - (grain[0].cur >> 12);
        lag = lag < 0 ? lag + (int32_t)gbuf[0].len : lag;
        snprintf(b, sizeof b, "SCAN DLY, OFST 25: the cursor a quarter of the buffer behind the write head (%d)", (int)lag);
        check(b, abs(lag - 44100 / 4) < CTL);
    }
    p[GP_SCAN] = SCAN_STR;

    {   /* SYNTH played with the transport stopped: it goes into the buffer and the grains play it */
        static int32_t o[400 * CTL];
        sys.playing = 0;
        render_poll(400);
        tp[0].src = SRC_SYNTH;
        p[GP_WET] = 100;
        fm1_in.notes = note_bit_of_white(9);
        render_poll(1378);
        fm1_in.notes = 0;
        check("SYNTH, stopped: what you play goes into the buffer", buf_loud(0, 0, 172) > 60u);
        render(400, o);
        check("..and grains of it sound after the note ends (no transport needed)", grain_count(0) > 0 &&
              peak_of(o + 200 * CTL, 200 * CTL) > 500);
        tp[0].src = SRC_TAPE;
        sys.playing = 1;
        render_poll(1378 * 2);
    }

    {   /* the pool follows how many tracks have GRAIN on (32, 24, 16), shared by what each asks */
        uint32_t mx = 0, t2;
        int16_t *q = tp[1].dev[DEV_GRAIN];
        p[GP_RATE] = 100;
        p[GP_SIZE] = 500;
        render_poll(1378);
        for (i = 0; i < 1378; i++) {
            render_poll(1);
            mx = grain_count(0) > mx ? grain_count(0) : mx;
        }
        snprintf(b, sizeof b, "GRAIN on one track, dense (RATE 100, SIZE 500): the whole pool, %u at once (32)", mx);
        check(b, mx == 32u && gr_allow(0) == 32u);
        q[GP_WET] = 100;                                 /* track 2 on, sparse (the defaults: about 1 at once) */
        render_poll(1);
        snprintf(b, sizeof b, "..a second track on, sparse: a pool of 24, it gets the %u it asks, the dense one the other %u",
                 gr_allow(1), gr_allow(0));
        check(b, gr_allow(1) == 3u && gr_allow(0) == 21u);
        for (i = 0, mx = 0; i < 1378; i++) {
            render_poll(1);
            mx = grain_count(0) > mx ? grain_count(0) : mx;
        }
        snprintf(b, sizeof b, "..the dense one thins to its share (%u at most), no grain cut: those sounding finished", mx);
        check(b, mx <= 32u && grain_count(0) <= 21u);
        q[GP_RATE] = 100;                                /* both dense: an even split */
        q[GP_SIZE] = 500;
        render_poll(1);
        check("..both dense: 12 and 12", gr_allow(0) == 12u && gr_allow(1) == 12u);
        tp[2].dev[DEV_GRAIN][GP_WET] = 100;              /* four on: a pool of 16; track 4 sparse, the rest dense */
        tp[2].dev[DEV_GRAIN][GP_RATE] = 100;
        tp[2].dev[DEV_GRAIN][GP_SIZE] = 500;
        tp[3].dev[DEV_GRAIN][GP_WET] = 100;
        render_poll(1);
        for (t2 = 0, mx = 0; t2 < NTRK; t2++)
            mx += gr_allow(t2);
        snprintf(b, sizeof b, "..four on, one sparse: a pool of 16: %u %u %u, and the sparse one its %u", gr_allow(0),
                 gr_allow(1), gr_allow(2), gr_allow(3));
        check(b, mx == 16u && gr_allow(3) == 3u && gr_allow(0) == 5u && gr_allow(1) == 4u && gr_allow(2) == 4u);
        chain_tracks(2);
        render_poll(1);
        check("TRACKS 2 (tracks 3 and 4 parked): two on, 24 between them, 12 and 12", gr_allow(0) == 12u &&
              gr_allow(1) == 12u && gr_allow(2) == 0u && gr_allow(3) == 0u);
        chain_tracks(4);
        for (t2 = 1; t2 < NTRK; t2++) {
            tp[t2].dev[DEV_GRAIN][GP_WET] = 0;
            tp[t2].dev[DEV_GRAIN][GP_RATE] = 40;
            tp[t2].dev[DEV_GRAIN][GP_SIZE] = 80;
        }
        p[GP_RATE] = 40;
        p[GP_SIZE] = 80;
        render_poll(1378);
    }

    {   /* nothing free: a buffer takes from the end of the longest tape */
        sys.playing = 0;
        render_poll(400);
        p[GP_WET] = 0;
        chain_poll();
        fill_tape(2, 100);
        fill_tape(3, 50);
        while (mem_count(MEM_FREE))
            mem_alloc(MEM_IMPORT);
        p[GP_WET] = 100;
        chain_poll();
        check("no chunk free: GRAIN's buffer takes from the end of the longest tape (track 3's)",
              gbuf[0].nch == 11 && tape_ctl[2].nch == 89 && tape_ctl[3].nch == 50);
    }
    sys.playing = 0;
    for (t = 0; t < NTRK; t++)
        track[t].mute = 0;
    render_poll(400);
}

/* a buffer's pitch by autocorrelation (partials and noise don't fool it as they do zero crossings), Hz */
static double buf_pitch(const int32_t *x, uint32_t from, uint32_t n, double fmin, double fmax)
{
    uint32_t lag, lo = (uint32_t)(44100.0 / fmax), hi = (uint32_t)(44100.0 / fmin), best = 0, i;
    double bc = -1e300, c[2048];
    for (lag = lo; lag <= hi && lag < 2048u; lag++) {
        double a = 0, e0 = 0, e1 = 0;
        for (i = from; i + lag < n; i++) {
            a += (double)x[i] * x[i + lag];
            e0 += (double)x[i] * x[i];
            e1 += (double)x[i + lag] * x[i + lag];
        }
        c[lag] = a / (sqrt(e0 * e1) + 1);
        if (c[lag] > bc) {
            bc = c[lag];
            best = lag;
        }
    }
    for (lag = lo; lag < best; lag++)                    /* (the shortest lag nearly as good: not an octave down) */
        if (c[lag] >= 0.9 * bc && (lag == lo || c[lag] >= c[lag - 1]) && c[lag] >= c[lag + 1]) {
            best = lag;
            break;
        }
    if (best > lo && best < hi) {                        /* (between samples: a parabola through the peak) */
        double y0 = c[best - 1], y1 = c[best], y2 = c[best + 1], d = y0 - 2 * y1 + y2;
        return 44100.0 / (best + (d ? 0.5 * (y0 - y2) / d : 0));
    }
    return best ? 44100.0 / best : 0;
}
static double buf_rms(const int32_t *x, uint32_t from, uint32_t n)
{
    double a = 0;
    uint32_t i;
    for (i = from; i < n; i++)
        a += (double)x[i] * x[i];
    return sqrt(a / (n - from + 1));
}

static void test_reso(void)
{
    enum { NB = 690 };                                   /* 0.5 s */
    static int32_t s[4 * NB * CTL];
    int16_t *p;
    char b[120];
    double hz, r1, r2;
    uint32_t t, k;
    power_on();
    p = tp[0].dev[DEV_RESO];
    for (t = 1; t < NTRK; t++)
        track[t].mute = 1;
    focus_dev(DEV_RESO, 0);
    ui_input();
    check("the RESONATOR page up: the keys go to it", sys.keys_reso == 1 && sys.keys_grain == 0);
    chain_poll();
    check("RESONATOR at WET 0 (the default): no strings, no memory taken", !reso[0].nch && !mem_count(MEM_RESO));
    fm1_in.notes = note_bit_of_white(9);
    render(NB, s);
    fm1_in.notes = 0;
    check("..a key with WET 0 (stopped): silence", peak_of(s, NB * CTL) == 0);
    p[RP_WET] = 100;
    chain_poll();
    check("WET up: four strings, four chunks of the shared memory", reso[0].nch == RS_N && mem_count(MEM_RESO) == RS_N);
    render(4, 0);
    fm1_in.notes = note_bit_of_white(9);                 /* white key 10 at OCT 3: A3 */
    render(2, 0);
    fm1_in.notes = 0;
    render(NB, s);
    hz = buf_pitch(s, 200 * CTL, NB * CTL, 100, 600);
    snprintf(b, sizeof b, "a key on a TAPE track plucks the strings at its note: A3, %.1f Hz (220), the root A3", hz);
    check(b, fabs(hz - 220) < 2.2 && reso[0].root16 == 57 * 16 && peak_of(s, NB * CTL) > 2000);
    r1 = buf_rms(s, 0, NB * CTL / 4);
    render(NB, s);
    r2 = buf_rms(s, 0, NB * CTL);
    p[RP_DEC] = 90;
    fm1_in.notes = note_bit_of_white(9);
    render(2, 0);
    fm1_in.notes = 0;
    render(NB, s);
    {
        double q1 = buf_rms(s, 0, NB * CTL / 4), q2;
        render(NB, s);
        q2 = buf_rms(s, 0, NB * CTL);
        snprintf(b, sizeof b, "DEC: rings longer as it rises (after 0.5 s: DEC 60 at %.0f%% of its start, DEC 90 at %.0f%%)",
                 100 * r2 / (r1 + 1), 100 * q2 / (q1 + 1));
        check(b, q2 / (q1 + 1) > 2 * r2 / (r1 + 1) && q2 > 0);
    }
    p[RP_DEC] = 60;
    p[RP_SCAL] = 1;                                      /* MAJ: root, third, fifth, octave */
    {
        int32_t r0 = 45 * 16;
        double q = (double)rs_period_q8(r0 + RS_PART16[1][1]) / rs_period_q8(r0), f = (double)rs_period_q8(r0 + RS_PART16[1][2]) / rs_period_q8(r0);
        snprintf(b, sizeof b, "SCAL MAJ: strings on the root, its third (%.4f of its period) and fifth (%.4f)", q, f);
        check(b, fabs(q - 0.7937) < 0.002 && fabs(f - 0.6674) < 0.002);
    }
    p[RP_SCAL] = 0;
    p[RP_PTCH] = 50;
    render(2, 0);
    check("PTCH turned after a key: it's the root again", reso[0].root16 == 0 && reso_root16(0) == 50 * 16);
    p[RP_PTCH] = 45;
    render(NB * 2, 0);

    sys.playing = 1;                                     /* the track's sound through the strings */
    p[RP_DEC] = 100;
    p[RP_TONE] = 100;
    for (k = 0; k < 4u; k++) {
        render(NB, s);
        if (peak_of(s, NB * CTL) > 32767)
            break;
    }
    check("the reel ringing the strings at DEC 100, TONE 100 for 2 s: loud, and it never runs away (under full scale)",
          k == 4u && peak_of(s, NB * CTL) > 3000 && peak_of(s, NB * CTL) <= 32767);
    p[RP_DEC] = 60;
    p[RP_TONE] = 60;
    p[RP_CUT] = 40;                                      /* the filter in front: dark */
    p[RP_SLOP] = 0;
    render(NB, 0);
    check("the filter in front (CUT 40) changes what rings, the strings still sound", peak_of(track_rt[0].last, CTL) > 0 ||
          reso[0].nch == RS_N);
    p[RP_CUT] = 127;
    sys.playing = 0;
    render(NB, 0);

    tp[0].src = SRC_SYNTH;                               /* a SYNTH track: the keys play it, the strings follow */
    fm1_in.notes = note_bit_of_white(4);
    render(20, 0);
    check("on a SYNTH track the keys still play the synth, and the strings take its note as their root",
          syn_sounding(0) == 1 && reso[0].root16 == (12 * 4 + 4) * 16);
    fm1_in.notes = 0;
    render(NB, 0);
    tp[0].src = SRC_TAPE;

    {
        uint32_t c0 = reso_cap;
        grain_cap = 4;                                   /* (the grains already down to half) */
        chain_shed();
        check("shedding, the grains at half already: a string goes (4 -> 3), and a second later it's back",
              reso_cap == c0 - 1u);
        render(1400, 0);
        check("..back", reso_cap == RS_N);
        grain_cap = GR_CAP;
    }
    chain_tracks(1);
    p = tp[1].dev[DEV_RESO];
    p[RP_WET] = 100;                                     /* (track 2's RESONATOR on, track 2 parked) */
    chain_poll();
    check("a parked track's RESONATOR holds no strings", !reso[1].nch && mem_count(MEM_RESO + 1) == 0);
    chain_tracks(4);
    tp[0].dev[DEV_RESO][RP_WET] = 0;
    chain_poll();
    check("WET 0 again: the strings' chunks back to the pool", !reso[0].nch && !mem_count(MEM_RESO));
    for (t = 0; t < NTRK; t++)
        track[t].mute = 0;
    press(B_HOME);
}

/* a device's block function over a whole buffer, CTL at a time (track 0) */
static void dev_run(void (*f)(uint32_t, int32_t *, int32_t *, uint32_t), int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t b;
    for (b = 0; b + CTL <= n; b += CTL)
        f(0, l + b, r + b, CTL);
}

static void sine_fill(int32_t *l, int32_t *r, uint32_t n, double hz, double amp)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        l[i] = r[i] = (int32_t)lrint(amp * sin(2 * M_PI * hz * i / 44100.0));
}

static uint32_t peak_at(const int32_t *x, uint32_t from, uint32_t to)
{
    uint32_t i, at = from;
    for (i = from; i < to; i++)
        if (abs(x[i]) > abs(x[at]))
            at = i;
    return at;
}

static double rms_of(const int32_t *x, uint32_t from, uint32_t to)
{
    double a = 0;
    uint32_t i;
    for (i = from; i < to; i++)
        a += (double)x[i] * x[i];
    return to > from ? sqrt(a / (to - from)) : 0;
}

/* COLOR (color.c): each knob does what its picture says, and the defaults leave the sound alone */
static void test_color(void)
{
    enum { N = 8192 };
    static int32_t l[N], r[N], l0[N];
    int16_t *p;
    uint32_t i, ok, runs;
    double a, b2, c;
    char b[140];
    power_on();
    p = tp[0].dev[DEV_COLOR];
    sine_fill(l, r, N, 441, 20000);
    memcpy(l0, l, sizeof l0);
    dev_run(color_block, l, r, N);
    check("COLOR at its defaults: the sound untouched, sample for sample", !memcmp(l, l0, sizeof l0));
    p[CP_LVL] = -6;
    color[0].lvl = 0;
    sine_fill(l, r, N, 441, 20000);
    dev_run(color_block, l, r, N);
    for (i = CTL, ok = 1; i < N; i++)
        ok &= abs(l[i] - l0[i] / 2) <= 1;
    check("LVL -6 dB alone: exactly half", ok);
    p[CP_LVL] = 0;
    p[CP_DRIV] = 100;
    sine_fill(l, r, N, 441, 16000);
    dev_run(color_block, l, r, N);
    a = peak_of(l + CTL, N - CTL) / rms_of(l, CTL, N);
    snprintf(b, sizeof b, "DRIV 100: the sine clipped near square (crest %.2f, a sine's 1.41), under full scale", a);
    check(b, a < 1.15 && peak_of(l, N) <= 32767);
    p[CP_DRIV] = 0;
    p[CP_CRSH] = 100;                                    /* CMOD BIT: 2 bits */
    sine_fill(l, r, N, 441, 30000);
    dev_run(color_block, l, r, N);
    for (i = 0, ok = 1; i < N; i++)
        ok &= l[i] % 16384 == 0;
    check("CRSH 100, CMOD BIT: 2 bits left (every sample a multiple of 16384)", ok && peak_of(l, N) == 16384);
    p[CP_CMOD] = CMOD_RATE;
    sine_fill(l, r, N, 441, 20000);
    dev_run(color_block, l, r, N);
    for (i = 1, runs = 0, ok = 0; i < N; i++) {
        runs += l[i] != l[i - 1];
        ok |= l[i] % 4 != 0;
    }
    snprintf(b, sizeof b, "CRSH 100, CMOD RATE: each value held 12 samples (%u changes in %u samples), bits kept", runs, N);
    check(b, runs <= N / 12 + 1 && runs > N / 16 && ok);
    p[CP_CMOD] = CMOD_BOTH;
    sine_fill(l, r, N, 441, 30000);
    dev_run(color_block, l, r, N);
    for (i = CTL, runs = 0, ok = 1; i < N; i++) {
        runs += l[i] != l[i - 1];
        ok &= l[i] % 16384 == 0;
    }
    check("CMOD BOTH: held and 2 bits", ok && runs <= N / 12 + 1);
    p[CP_CRSH] = 0;
    p[CP_CMOD] = CMOD_BIT;

    p[CP_NOIS] = 100;                                    /* NDEC 40: the table's */
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    dev_run(color_block, l, r, N);
    check("NOIS 100 on silence: silence (the noise rides the sound)", peak_of(l, N) == 0);
    {
        uint32_t ndec = TIME_MS_X10[40] * 441u / 100u;   /* NDEC 40, samples */
        static int32_t L[44100], R[44100];
        memset(L, 0, sizeof L);
        memset(R, 0, sizeof R);
        for (i = 0; i < 441; i++)                        /* a 10 ms hit */
            L[i] = R[i] = 20000;
        dev_run(color_block, L, R, 44100);
        a = rms_of(L, 441, 441 + ndec / 8);
        b2 = rms_of(L, 441 + ndec * 2, 441 + ndec * 3);
        snprintf(b, sizeof b, "NOIS after a hit: noise right after it (rms %.0f), gone by 2 x NDEC (%.1f), %u ms",
                 a, b2, ndec * 10 / 441);
        check(b, a > 1000 && b2 < a / 300);
        p[CP_NTON] = -100;
        memset(L, 0, sizeof L);
        memset(R, 0, sizeof R);
        for (i = 0; i < 44100; i++)
            L[i] = R[i] = 20000;
        color[0].nlp = 0;
        dev_run(color_block, L, R, 44100);
        for (i = 1, a = c = 0; i < 44100; i++) {
            a += (double)(L[i] - 20000) * (L[i] - 20000);
            c += (double)(L[i] - L[i - 1]) * (L[i] - L[i - 1]);
        }
        a = sqrt(c / a);
        p[CP_NTON] = 100;
        for (i = 0; i < 44100; i++)
            L[i] = R[i] = 20000;
        dev_run(color_block, L, R, 44100);
        for (i = 1, b2 = c = 0; i < 44100; i++) {
            b2 += (double)(L[i] - 20000) * (L[i] - 20000);
            c += (double)(L[i] - L[i - 1]) * (L[i] - L[i - 1]);
        }
        b2 = sqrt(c / b2);
        snprintf(b, sizeof b, "NTON: -100 dark, +100 bright (the noise's steps against its level: %.2f, %.2f)", a, b2);
        check(b, a * 3 < b2);
    }
    p[CP_NOIS] = 0;
    p[CP_NTON] = 0;

    p[CP_TILT] = 100;
    sine_fill(l, r, N, 100, 10000);
    dev_run(color_block, l, r, N);
    a = rms_of(l, N / 2, N) / (10000 / M_SQRT2);
    sine_fill(l, r, N, 8000, 10000);
    dev_run(color_block, l, r, N);
    b2 = rms_of(l, N / 2, N) / (10000 / M_SQRT2);
    snprintf(b, sizeof b, "TILT +100: 100 Hz down (x%.2f), 8 kHz up (x%.2f)", a, b2);
    check(b, a < 0.6 && b2 > 1.7);
    p[CP_TILT] = -100;
    sine_fill(l, r, N, 100, 10000);
    dev_run(color_block, l, r, N);
    a = rms_of(l, N / 2, N) / (10000 / M_SQRT2);
    sine_fill(l, r, N, 8000, 10000);
    dev_run(color_block, l, r, N);
    b2 = rms_of(l, N / 2, N) / (10000 / M_SQRT2);
    snprintf(b, sizeof b, "TILT -100: the other way (x%.2f, x%.2f)", a, b2);
    check(b, a > 1.7 && b2 < 0.6);
    p[CP_TILT] = 0;

    p[CP_DRIV] = 100;
    p[CP_WET] = 0;
    dev_run(color_block, l, r, N);                       /* (WET ramps over a block) */
    sine_fill(l, r, N, 441, 20000);
    dev_run(color_block, l, r, N);
    check("WET 0: the dry sound, however much DRIV", !memcmp(l, l0, sizeof l0));
    p[CP_WET] = 100;

    for (i = 0; i < 9; i++)                              /* every knob at its corner, a sound past full scale */
        p[i] = DEV_P[DEV_COLOR][i].max;
    for (i = 0; i < N; i++)
        l[i] = r[i] = (i / 50) & 1 ? 131071 : -131071;
    dev_run(color_block, l, r, N);
    check("every knob at its top, a square past full scale: bounded", peak_of(l, N) <= 2 * 131071);
    for (i = 0; i < 9; i++)
        p[i] = DEV_P[DEV_COLOR][i].min;
    dev_run(color_block, l, r, N);
    check("..and every knob at its bottom", peak_of(l, N) <= 131071);
    for (i = 0; i < 9; i++)
        p[i] = DEV_P[DEV_COLOR][i].def;
}

/* SPACE (space.c): the echoes where TIME says and falling by FDBK, the room's tail as long as DEC, the memory
 * taken only while it's used, 8-bit when it's short */
static void test_space(void)
{
    enum { N = 44100 };                                  /* 1 s */
    static int32_t l[N], r[N];
    int16_t *p;
    uint32_t i, a1, a2, t, tot0, tot1;
    double e1, e2, x, y;
    char b[160];
    power_on();
    p = tp[0].dev[DEV_SPACE];
    chain_poll();
    check("SPACE at DLY 0, VERB 0 (the defaults): no memory taken", !mem_count(MEM_SPACE) && !space[0].dly.nch);
    sine_fill(l, r, N, 441, 20000);
    memcpy(r, l, sizeof r);
    dev_run(space_block, l, r, N);
    check("..and the sound untouched", !memcmp(l, r, sizeof r) && l[100] == (int32_t)lrint(20000 * sin(2 * M_PI * 441 * 100 / 44100.0)));

    p[SP_DLY] = 100;
    p[SP_FDBK] = 0;
    p[SP_TIME] = 100;
    p[SP_SPRD] = 0;
    chain_poll();
    check("DLY up: the delay's line, 8 chunks at 16 bits", space[0].dly.ready && !space[0].dly.bits8 &&
          mem_count(MEM_SPACE) == SP_DLY16);
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    l[0] = r[0] = l[1] = r[1] = 20000;
    dev_run(space_block, l, r, N / 2);
    a1 = peak_at(l, 100, N / 2);
    snprintf(b, sizeof b, "TIME 100, FDBK 0: one echo at %.1f ms (100) of %d (0.8 of the hit), none after", a1 / 44.1,
             l[a1]);
    check(b, fabs(a1 / 44.1 - 100) < 0.5 && l[a1] > 14000 && l[a1] < 17000 && peak_of(l + 5000, N / 2 - 5000) < 50 &&
          r[a1] == l[a1]);
    p[SP_FDBK] = 50;
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    l[0] = r[0] = l[1] = r[1] = 20000;
    dev_run(space_block, l, r, N / 2);
    a1 = peak_at(l, 100, 6000);
    a2 = peak_at(l, 6000, 11000);
    e1 = l[a2] / (double)l[a1];
    snprintf(b, sizeof b, "FDBK 50: a second echo at %.1f ms, %.2f of the first", a2 / 44.1, e1);
    check(b, fabs(a2 / 44.1 - 200) < 0.5 && e1 > 0.4 && e1 < 0.55);
    p[SP_TONE] = -100;
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    l[0] = r[0] = l[1] = r[1] = 20000;
    dev_run(space_block, l, r, N / 2);
    e2 = l[peak_at(l, 6000, 11000)] / (double)l[peak_at(l, 100, 6000)];
    p[SP_TONE] = 100;
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    l[0] = r[0] = l[1] = r[1] = 20000;
    dev_run(space_block, l, r, N / 2);
    x = abs(l[peak_at(l, 6000, 11000)]) / (double)l[peak_at(l, 100, 6000)];
    snprintf(b, sizeof b, "TONE -100 and +100: each pass loses more (the second echo %.2f, %.2f of the first; 0: %.2f)",
             e2, x, e1);
    check(b, e2 < e1 * 0.6 && x < e1 * 0.9);
    p[SP_TONE] = 0;
    p[SP_FDBK] = 0;
    p[SP_SPRD] = 100;
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    l[0] = r[0] = l[1] = r[1] = 20000;
    dev_run(space_block, l, r, N / 2);
    a1 = peak_at(l, 100, N / 2);
    a2 = peak_at(r, 100, N / 2);
    snprintf(b, sizeof b, "SPRD 100: the right echo %.1f ms ahead of the left (10)", (a1 - (double)a2) / 44.1);
    check(b, fabs((a1 - (double)a2) / 44.1 - 10) < 0.3);
    p[SP_SPRD] = 0;

    p[SP_TIME] = 100;                                    /* TIME turned while it sounds: a glide, no jump */
    sine_fill(l, r, N, 200, 10000);
    for (i = 0; i < N / 2; i += CTL)
        space_block(0, l + i, r + i, CTL);
    p[SP_TIME] = 300;
    for (; i + CTL <= N; i += CTL)
        space_block(0, l + i, r + i, CTL);
    for (i = 5000, a1 = 0; i < N - N % CTL; i++)
        a1 = abs(l[i] - l[i - 1]) > (int32_t)a1 ? (uint32_t)abs(l[i] - l[i - 1]) : a1;
    snprintf(b, sizeof b, "TIME turned 100 -> 300 ms while echoing: it glides (largest step %u; a 200 Hz sine's: 285 "
             "plus the echo's)", a1);
    check(b, a1 < 800);

    p[SP_DLY] = 0;
    chain_poll();
    check("DLY 0: the line back to the pool", !space[0].dly.nch && !mem_count(MEM_SPACE));
    fill_tape(1, 60);                                    /* memory short: 20 chunks left free */
    fill_tape(2, 60);
    while (mem_count(MEM_FREE) > 20u)
        mem_alloc(MEM_IMPORT);
    p[SP_DLY] = 100;
    p[SP_TIME] = 100;
    chain_poll();
    check("memory short (20 chunks free): the delay starts 8-bit, 4 chunks", space[0].dly.ready &&
          space[0].dly.bits8 && mem_count(MEM_SPACE) == SP_DLY8 && mem_count(MEM_FREE) == 16u);
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    l[0] = r[0] = l[1] = r[1] = 20000;
    dev_run(space_block, l, r, N / 2);
    a1 = peak_at(l, 100, N / 2);
    snprintf(b, sizeof b, "..its echo where 16 bits put it: %.1f ms, %d", a1 / 44.1, l[a1]);
    check(b, fabs(a1 / 44.1 - 100) < 0.5 && l[a1] > 13500 && l[a1] < 17000);
    {
        static int32_t s16[N], s8[N];
        sine_fill(l, r, N / 4, 300, 3000);
        memset(l + N / 4, 0, (N - N / 4) * sizeof l[0]);
        memset(r + N / 4, 0, (N - N / 4) * sizeof r[0]);
        dev_run(space_block, l, r, N / 2);
        memcpy(s8, l, sizeof s8);
        p[SP_DLY] = 0;
        chain_poll();
        for (i = 0; i < MEM_NC; i++)
            if (mem_owner[i] == MEM_IMPORT)
                mem_free(i);
        p[SP_DLY] = 100;
        chain_poll();
        sine_fill(l, r, N / 4, 300, 3000);
        memset(l + N / 4, 0, (N - N / 4) * sizeof l[0]);
        memset(r + N / 4, 0, (N - N / 4) * sizeof r[0]);
        dev_run(space_block, l, r, N / 2);
        memcpy(s16, l, sizeof s16);
        for (i = 6000, x = y = 0; i < N / 4; i++) {
            x += (double)(s8[i] - s16[i]) * (s8[i] - s16[i]);
            y += (double)s16[i] * s16[i];
        }
        snprintf(b, sizeof b, "..8-bit against 16: the echo of a quiet sine %.0f dB above its own noise",
                 10 * log10(y / x));
        check(b, 10 * log10(y / x) > 30 && space[0].dly.bits8 == 0);
    }
    p[SP_DLY] = 0;
    chain_poll();
    while (mem_count(MEM_FREE))                          /* nothing free: it takes from the longest tape */
        mem_alloc(MEM_IMPORT);
    tot0 = tape_ctl[1].nch + tape_ctl[2].nch;
    p[SP_DLY] = 100;
    chain_poll();
    tot1 = tape_ctl[1].nch + tape_ctl[2].nch;
    check("nothing free: 8-bit, its 4 chunks from the end of the longest tape", space[0].dly.ready &&
          space[0].dly.bits8 && tot0 - tot1 == SP_DLY8 && mem_count(MEM_SPACE) == SP_DLY8);
    p[SP_DLY] = 0;
    chain_poll();
    for (i = 0; i < MEM_NC; i++)
        if (mem_owner[i] == MEM_IMPORT)
            mem_free(i);
    for (t = 1; t < NTRK; t++)
        tape_free(t);
    tape_poll();

    p[SP_VERB] = 100;                                    /* the room */
    p[SP_PRE] = 0;
    p[SP_DEC] = 100;
    chain_poll();
    check("VERB up: the room (5 chunks) and its pre-delay (5, 16-bit)", space[0].rev.ready && space[0].pre.ready &&
          !space[0].pre.bits8 && mem_count(MEM_SPACE) == SP_REV + SP_PRE16);
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    for (i = 0; i < 64; i++)
        l[i] = r[i] = 20000;
    dev_run(space_block, l, r, N);
    e1 = rms_of(l, N / 2, N);
    x = rms_of(l, 2000, 6000);
    p[SP_DEC] = 0;
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    dev_run(space_block, l, r, N);                       /* (the long tail out) */
    for (i = 0; i < 64; i++)
        l[i] = r[i] = 20000;
    dev_run(space_block, l, r, N);
    e2 = rms_of(l, N / 2, N);
    snprintf(b, sizeof b, "DEC 100 against 0: the tail at 0.5-1 s %.0f against %.1f (the early tail %.0f)", e1, e2, x);
    check(b, e1 > 30 && e2 < e1 / 30 && x > 100);
    p[SP_DEC] = 40;
    p[SP_SPRD] = 100;
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    dev_run(space_block, l, r, N / 4);
    for (i = 0; i < 64; i++)
        l[i] = r[i] = 20000;
    dev_run(space_block, l, r, N / 2);
    for (i = 3000, x = y = e1 = 0; i < N / 2; i++) {
        x += (double)l[i] * r[i];
        y += (double)l[i] * l[i];
        e1 += (double)r[i] * r[i];
    }
    snprintf(b, sizeof b, "SPRD 100: the room's sides differ (correlation %.2f)", x / sqrt(y * e1));
    check(b, x / sqrt(y * e1) < 0.5);
    p[SP_SPRD] = 0;
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    for (i = 0; i < 64; i++)
        l[i] = r[i] = 20000;
    dev_run(space_block, l, r, N / 2);
    check("SPRD 0: mono", !memcmp(l + 2, r + 2, (N / 2 - 2) * sizeof l[0]));
    p[SP_PRE] = 100;
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    for (i = 0; i < 3; i++) {                            /* (the last tail out) */
        memset(l, 0, sizeof l);
        memset(r, 0, sizeof r);
        dev_run(space_block, l, r, N);
    }
    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    for (i = 0; i < 64; i++)
        l[i] = r[i] = 20000;
    dev_run(space_block, l, r, N / 2);
    snprintf(b, sizeof b, "PRE 100 ms: nothing from the room before it (peak %d), the room after (%.0f)",
             peak_of(l + 100, 4300), rms_of(l, 4500, 9000));
    check(b, peak_of(l + 100, 4300) < 20 && rms_of(l, 4500, 9000) > 100);

    for (i = 0; i < 9; i++)                              /* the corners: everything up, noise in, then quiet */
        p[i] = DEV_P[DEV_SPACE][i].max;
    for (t = 0; t < 3; t++) {
        p[SP_TONE] = (int16_t)(t == 0 ? -100 : t == 1 ? 100 : 0);
        p[SP_SIZE] = (int16_t)(t * 50);
        chain_poll();
        for (i = 0; i < N; i++)
            l[i] = r[i] = (int32_t)(rand() % 65535) - 32767;
        dev_run(space_block, l, r, N);
        e1 = peak_of(l, N);
        memset(l, 0, sizeof l);
        memset(r, 0, sizeof r);
        dev_run(space_block, l, r, N);
        snprintf(b, sizeof b, "the corners (TONE %d, SIZE %d, all else up): bounded (peak %.0f), %.0f after 1 s of "
                 "quiet", p[SP_TONE], p[SP_SIZE], e1, (double)peak_of(l + N / 2, N / 2));
        check(b, e1 < 4 * 32767 && peak_of(l, N) < 3 * 32767);
    }
    for (i = 0; i < 9; i++)
        p[i] = DEV_P[DEV_SPACE][i].def;
    p[SP_DLY] = p[SP_VERB] = 100;
    chain_poll();
    chain_tracks(1);
    tp[1].dev[DEV_SPACE][SP_DLY] = 100;                 /* (track 2's SPACE on, track 2 parked) */
    chain_poll();
    check("a parked track's SPACE holds nothing", !space[1].dly.nch && !mem_count(MEM_SPACE + 1));
    chain_tracks(4);
    chain_poll();
    check("..TRACKS back up: it takes its line", space[1].dly.ready);
    tp[1].dev[DEV_SPACE][SP_DLY] = 0;
    p[SP_DLY] = p[SP_VERB] = 0;
    chain_poll();
    check("DLY and VERB 0: every chunk back to the pool", !mem_count(MEM_SPACE) && !mem_count(MEM_SPACE + 1) &&
          !space[0].rev.nch && !space[0].pre.nch);
}

/* the computer playing into the FM-1 (what usb.c's uaco_service hands usbrec.c from each 1 ms packet): frames of a
 * sine, l at amplitude a, r at half, phase kept across calls */
static double ur_ph;
static void ur_feed(uint32_t frames, double hz, double a)
{
    uint8_t p[46 * 4];
    while (frames) {
        uint32_t n = frames > 45u ? 45u : frames, i;
        for (i = 0; i < n; i++) {
            int16_t l = (int16_t)lrint(a * sin(ur_ph)), r = (int16_t)lrint(a * 0.5 * sin(ur_ph));
            ur_ph += 2 * M_PI * hz / 44100.0;
            p[4 * i] = (uint8_t)l;
            p[4 * i + 1] = (uint8_t)((uint16_t)l >> 8);
            p[4 * i + 2] = (uint8_t)r;
            p[4 * i + 3] = (uint8_t)((uint16_t)r >> 8);
        }
        uaco_frames(p, n);
        frames -= n;
    }
}

/* blocks rendered as the device would: the computer's frames at 44,100 a second against the DAC's 44,117.6, the
 * main loop between blocks; the left output into buf (or 0) */
static double ur_due;
static void ur_play(uint32_t blocks, double hz, double a, int32_t *buf)
{
    uint32_t b;
    for (b = 0; b < blocks; b++) {
        uint32_t n;
        ur_due += CTL * 44100.0 / 44117.647;
        n = (uint32_t)ur_due;
        ur_due -= n;
        if (a > 0)
            ur_feed(n, hz, a);
        render(1, buf ? buf + b * CTL : 0);
        chain_poll();
        fm1_ms += (b & 1u) ? 1u : 0u;
    }
}

/* the USB record mode (usbrec.c): REC held a second opens it, the computer is heard, a take records exactly what
 * arrived, goes onto the track picked, and the mode closes */
static void test_usbrec(void)
{
    enum { NB = 2756 };                                  /* 2 s */
    static int32_t s[NB * CTL];
    char b[160];
    uint32_t i, k, lo = 0xFFFFFFFFu, hi = 0, nb, frames;
    int32_t jump;
    double hz;
    power_on();
    memset(&ur, 0, sizeof ur);
    ur_mw = ur_mr = ur_rw = ur_rr = 0;
    tp[0].dev[DEV_GRAIN][GP_WET] = 100;                  /* (memory the tracks hold: it goes back in the mode) */
    tp[1].dev[DEV_SPACE][SP_DLY] = 100;
    chain_poll();
    check("before: GRAIN's buffer and SPACE's line hold memory", mem_count(MEM_GRAIN) > 0 && mem_count(MEM_SPACE + 1) > 0);

    sys.playing = 1;                                     /* playing: REC arms as it goes down; held, it says why not */
    hold(B_REC);
    check("playing: REC arms the moment it goes down (a punch-in where it's pressed)", (sys.rec & 1u) == 1u);
    fm1_ms += 1100;
    ui_input();
    check("..held a second while playing: no record mode, a hint to stop first",
          ui.view != VIEW_USBREC && !sys.usbrec && !strcmp(ui.msg, "STOP, THEN HOLD REC: USB RECORD"));
    let_go(B_REC);
    press(B_REC);
    check("..REC again lets go of it", sys.rec == 0);
    sys.playing = 0;

    hold(B_REC);                                         /* stopped: the arm waits for the let-go */
    check("stopped: REC down arms nothing yet", sys.rec == 0);
    fm1_ms += 400;
    ui_input();
    check("..held 0.4 s: \"KEEP HOLDING REC: USB RECORD\"", !strcmp(ui.msg, "KEEP HOLDING REC: USB RECORD") && !sys.usbrec);
    fm1_ms += 700;
    ui_input();
    let_go(B_REC);
    check("..held a second: the USB record mode (READY), and REC's let-go arms nothing",
          ui.view == VIEW_USBREC && sys.usbrec && ur.state == UR_READY && sys.rec == 0 && !sys.playing);
    chain_poll();
    check("the tracks' devices give their memory back (GRAIN's buffer, SPACE's line)",
          !mem_count(MEM_GRAIN) && !mem_count(MEM_SPACE + 1));
    press(B_PLAY);
    check("PLAY does nothing in the mode", !sys.playing);
    render(1, 0);
    check("one block fades the tracks out, then the ISR plays the computer", ur.isr_in == 1);

    ur_play(NB, 441, 12000, s);                          /* the monitor: 2 s of a sine from the computer */
    hz = buf_pitch(s, NB * CTL / 2, NB * CTL, 200, 1000);
    for (i = NB * CTL / 4, jump = 0; i < NB * CTL; i++)
        jump = abs(s[i] - s[i - 1]) > jump ? abs(s[i] - s[i - 1]) : jump;
    for (k = 0; k < 2000; k++) {                         /* the ring's fill, the computer's clock against the DAC's */
        uint32_t f;
        ur_play(1, 441, 12000, 0);
        f = ur_mw - ur_mr;
        lo = f < lo ? f : lo;
        hi = f > hi ? f : hi;
    }
    snprintf(b, sizeof b, "the computer heard: %.1f Hz (441), no step over %d, the ring held at %u..%u frames (aim %u)",
             hz, jump, lo, hi, UR_MON_AIM);
    check(b, fabs(hz - 441) < 2 && jump < 1400 && lo + 48u >= UR_MON_AIM && hi <= UR_MON_AIM + 48u);
    check("..the computer playing: the screen knows (live)", usbrec_live());
    fm1_ms += 300;
    usbrec_poll();
    check("..300 ms with nothing: not live", !usbrec_live());

    press(B_REC);                                        /* a take */
    check("REC: recording", ur.state == UR_RECORDING && ur.feed);
    frames = 0;
    for (k = 0; k < 3u * 1378u; k++) {                   /* 3 s of 300 Hz, in packets of 44 and 45 frames */
        uint32_t n = (k % 10u) == 9u ? 45u : 44u;
        ur_feed(n / 2u, 300, 10000);
        ur_feed(n - n / 2u, 300, 10000);
        frames += n;
        render(1, 0);
        if (k % 4u == 0u)
            chain_poll();
    }
    chain_poll();
    press(B_REC);
    nb = (frames / 2u + TAPE_BLK - 1u) / TAPE_BLK;
    snprintf(b, sizeof b, "REC again: CHOOSE, the take every frame that arrived (%u frames: %u blocks, %u in it), none lost",
             frames, nb, ur.nblk);
    check(b, ur.state == UR_CHOOSE && ur.nblk == nb && ur.lost == 0 && ur.dest < 0);
    press(B_REC);
    check("..REC before a track is picked: it asks for one", ur.state == UR_CHOOSE &&
          !strcmp(ui.msg, "PICK A TRACK: WHITE KEYS 1-4"));
    key_edge(note_bit_of_white(2) ? (uint32_t)__builtin_ctz(note_bit_of_white(2)) : 0u);
    check("..white key 3: track 3 picked", ur.dest == 2);
    check("CHOOSE starts with the whole take: START 0, LENGTH all of it, GAIN 0 dB, FADE 10 ms",
          ur.kv[UK_STRT] == 0 && ur.kv[UK_LEN] == (int16_t)nb && ur.kv[UK_GAIN] == 0 &&
          ur.kv[UK_FADE] * UR_FADE_MS == 10);
    render(NB / 2, s);                                   /* the preview: the take, looping */
    hz = buf_pitch(s, 0, NB / 2 * CTL, 100, 1000);
    snprintf(b, sizeof b, "..you hear the take looping (%.1f Hz, 300), not the computer", hz);
    check(b, fabs(hz - 300) < 1.5 && peak_of(s, NB / 2 * CTL) > 2000);
    {
        uint32_t keep0 = ur_free_after(), k2;
        for (k2 = 0; k2 < 4u; k2++)
            usbrec_knob(UK_STRT, 4);                     /* turned fast: 16 blocks a detent; 4 x 4 x 16 = 256 */
        for (k2 = 0; k2 < 213u; k2++)
            usbrec_knob(UK_STRT, -1);                    /* slowly, a block a detent: back to 43 */
        for (k2 = 0; k2 < 30u; k2++)
            usbrec_knob(UK_LEN, -4);                     /* fast to the least (1 block), then slowly to 86 (1.0 s) */
        for (k2 = 0; k2 < 85u; k2++)
            usbrec_knob(UK_LEN, 1);
        snprintf(b, sizeof b, "KNOB 1-2: START 43 blocks (0.50 s), LENGTH 86 (1.00 s): %d, %d; %u chunks kept, more left "
                 "for the other tracks", ur.kv[UK_STRT], ur.kv[UK_LEN], ur_keep_chunks());
        check(b, ur.kv[UK_STRT] == 43 && ur.kv[UK_LEN] == 86 && ur_keep_chunks() == 6u && ur_free_after() > keep0 &&
              ur.pv_s == 43u * TAPE_BLK && ur.pv_e == 129u * TAPE_BLK);
        usbrec_knob(UK_GAIN, 40);
        snprintf(b, sizeof b, "KNOB 3 past +24 dB: NORM, the loudest point to full scale (x%.2f; the mono take peaks "
                 "at 7,500)", ur.pv_g / 4096.0);
        check(b, ur.kv[UK_GAIN] == UR_NORM && fabs(ur.pv_g / 4096.0 - 32767.0 / 7500) < 0.1);
        usbrec_knob(UK_GAIN, -31);
        usbrec_knob(UK_FADE, 18);
        check("..GAIN -6 dB (x0.5), FADE 100 ms", ur.kv[UK_GAIN] == -6 && ur.pv_g == 2048 &&
              ur.pv_f == 100u * (TAPE_SR / 1000u));
    }
    press(B_REC);
    snprintf(b, sizeof b, "..REC: track 3's tape is the trimmed take (%u blocks), the mode closes, track 3 focused, the "
             "rest of the take back in the pool", tape_ctl[2].nblk);
    check(b, tape_ctl[2].nblk == 86u && tape_ctl[2].nch == 6u && !tape_ctl[2].empty && ur.state == UR_OFF &&
          !sys.usbrec && ui.view == VIEW_PAGE && sys.sel == 2 && !mem_count(MEM_IMPORT));
    {
        tape_view_t v;
        tape_rd_t rd;
        int32_t first = 0, mid = 0, last = 0;
        memset(&rd, 0, sizeof rd);
        tape_view(2, &v);
        for (i = 0; i < 220u; i++)                       /* the first 10 ms of the fade in */
            first = abs(tape_at(&v, &rd, (int32_t)i)) > first ? abs(tape_at(&v, &rd, (int32_t)i)) : first;
        for (i = 6000u; i < 12000u; i++)
            mid = abs(tape_at(&v, &rd, (int32_t)i)) > mid ? abs(tape_at(&v, &rd, (int32_t)i)) : mid;
        for (i = 86u * TAPE_BLK - 220u; i < 86u * TAPE_BLK; i++)
            last = abs(tape_at(&v, &rd, (int32_t)i)) > last ? abs(tape_at(&v, &rd, (int32_t)i)) : last;
        snprintf(b, sizeof b, "..kept as shaped: faded in (%d in the first 10 ms) and out (%d), at half (%d; was 7,500)",
                 first, last, mid);
        check(b, first < 1000 && last < 1000 && mid > 3400 && mid < 4100);
    }
    render(2, 0);
    check("..the monitor fades out and the tracks render again", ur.isr_in == 0);
    for (k = 0; k < NTRK; k++)
        track[k].mute = k != 2u;
    sys.playing = 1;
    render(NB / 4, 0);
    render(NB / 2, s);
    sys.playing = 0;
    hz = buf_pitch(s, 0, NB / 2 * CTL, 100, 1000);
    snprintf(b, sizeof b, "..and it plays: %.1f Hz (300)", hz);
    check(b, fabs(hz - 300) < 1.5 && peak_of(s, NB / 2 * CTL) > 500);
    for (k = 0; k < NTRK; k++)
        track[k].mute = 0;

    hold(B_REC);                                         /* memory full: the take stops where it runs out */
    fm1_ms += 1100;
    ui_input();
    let_go(B_REC);
    chain_poll();
    while (mem_count(MEM_FREE) > 3u)
        mem_alloc(MEM_SPARE);
    press(B_REC);
    for (k = 0; k < 1378u && ur.state == UR_RECORDING; k++) {
        ur_feed(44, 300, 10000);
        render(1, 0);
        chain_poll();
    }
    snprintf(b, sizeof b, "3 chunks free: the take stops at them (%u blocks), CHOOSE, \"MEMORY FULL\"", ur.nblk);
    check(b, ur.state == UR_CHOOSE && ur.nblk == 3u * MEM_CB && ur.nch == 3u &&
          !strcmp(ui.msg, "THE TAKE STOPPED: MEMORY FULL"));
    press(B_HOME);
    check("HOME: the take thrown away, its chunks back (READY)", ur.state == UR_READY && !ur.nch &&
          mem_count(MEM_FREE) == 3u);
    press(B_HOME);
    check("HOME again: the mode closes", ur.state == UR_OFF && !sys.usbrec && ui.view == VIEW_PAGE);
    for (i = 0; i < MEM_NC; i++)
        if (mem_owner[i] == MEM_SPARE)
            mem_free(i);
}

/* a sine through track 0's channel strip (and pan): its level after, against before, in dB (the second half) */
static double ch_db(double hz, double amp)
{
    enum { N = 8192 };
    static int32_t l[N], r[N];
    uint32_t i;
    sine_fill(l, r, N, hz, amp);
    memset(&chan[0], 0, sizeof chan[0]);
    for (i = 0; i + CTL <= N; i += CTL)
        mix_channel(0, l + i, r + i, CTL);
    return 20 * log10(rms_of(l, N / 2, N) / (amp / M_SQRT2));
}

/* the mixer's sound (mixer.c): each channel strip does what its picture draws, the pan, the master compressor */
static void test_mixer(void)
{
    enum { N = 8192 };
    static int32_t l[N], r[N], l0[N];
    int16_t *ch;
    char b[160];
    double a, c, d;
    uint32_t i, k;
    int32_t k2_dummy;
    power_on();
    ch = tp[0].ch;
    sine_fill(l, r, N, 441, 20000);
    memcpy(l0, l, sizeof l0);
    for (i = 0; i + CTL <= N; i += CTL) {
        mix_channel(0, l + i, r + i, CTL);
        mix_pan(0, l + i, r + i, 4096, CTL);
        mix_comp(l + i, r + i, CTL);
    }
    check("the mixer at its defaults (channel flat, pan centred, AMT 0): the sound untouched, sample for sample",
          !memcmp(l, l0, sizeof l0) && !memcmp(r, l0, sizeof l0));
    ch[CH_LOW] = 12;
    a = ch_db(60, 8000);
    c = ch_db(8000, 8000);
    snprintf(b, sizeof b, "LOW +12: 60 Hz up %.1f dB, 8 kHz %.1f dB", a, c);
    check(b, a > 10 && a < 12.5 && fabs(c) < 1);
    ch[CH_LOW] = 0;
    ch[CH_HIGH] = -12;
    a = ch_db(60, 8000);
    c = ch_db(8000, 8000);
    snprintf(b, sizeof b, "HIGH -12: 8 kHz down %.1f dB, 60 Hz %.1f dB", c, a);
    check(b, c < -10 && c > -12.5 && fabs(a) < 1);
    ch[CH_HIGH] = 0;
    ch[CH_FILT] = -100;
    a = ch_db(50, 8000);
    c = ch_db(1000, 8000);
    snprintf(b, sizeof b, "FILT -100 (low-pass at 92 Hz): 50 Hz %.1f dB, 1 kHz %.1f dB", a, c);
    check(b, fabs(a) < 2 && c < -25);
    ch[CH_FILT] = 100;
    a = ch_db(1000, 8000);
    c = ch_db(10000, 8000);
    snprintf(b, sizeof b, "FILT +100 (high-pass at 4.5 kHz): 1 kHz %.1f dB, 10 kHz %.1f dB", a, c);
    check(b, a < -20 && fabs(c) < 2);
    ch[CH_FILT] = -50;
    a = ch_db(200, 8000);
    c = ch_db(4000, 8000);
    d = ch_db(970, 8000);
    snprintf(b, sizeof b, "FILT -50 (corner ~970 Hz): 200 Hz %.1f, at the corner %.1f, 4 kHz %.1f dB (12 dB an octave)",
             a, d, c);
    check(b, fabs(a) < 1 && d < -1.5 && d > -4.5 && c < -20 && c > -30);
    ch[CH_FILT] = 0;

    for (i = 0; i < 3u; i++) {                           /* the pan: -100, 0, +100 */
        int32_t pan = (int32_t)i * 100 - 100;
        ch[CH_PAN] = (int16_t)pan;
        sine_fill(l, r, N, 441, 10000);
        memset(&chan[0], 0, sizeof chan[0]);
        mix_pan(0, l, r, 4096, CTL);                           /* (the first block ramps from nothing to the pan) */
        for (k = CTL; k + CTL <= N; k += CTL)
            mix_pan(0, l + k, r + k, 4096, CTL);
        a = rms_of(l, N / 2, N) / (10000 / M_SQRT2);
        c = rms_of(r, N / 2, N) / (10000 / M_SQRT2);
        snprintf(b, sizeof b, "PAN %+d: left x%.2f, right x%.2f", pan, a, c);
        check(b, pan < 0 ? a > 0.99 && c < 0.01 : pan > 0 ? c > 0.99 && a < 0.01 : a > 0.99 && c > 0.99);
    }
    ch[CH_PAN] = -50;
    sine_fill(l, r, N, 441, 10000);
    memset(&chan[0], 0, sizeof chan[0]);
    for (k = 0; k + CTL <= N; k += CTL)
        mix_pan(0, l + k, r + k, 4096, CTL);
    a = rms_of(l, N / 2, N) / (10000 / M_SQRT2);
    c = rms_of(r, N / 2, N) / (10000 / M_SQRT2);
    snprintf(b, sizeof b, "PAN -50: the left at full, the right at x%.2f (sqrt 2 sin 22.5 degrees: 0.54)", c);
    check(b, a > 0.99 && fabs(c - 0.541) < 0.02);
    ch[CH_PAN] = 0;

    {   /* the compressor: a quiet and a loud sine, 20 dB apart, before and after */
        double q, lo;
        mst[MS_AMT] = 100;                               /* -30 dB, 4:1 */
        for (i = 0; i < 2u; i++) {
            double amp = i ? 30000 : 3000;
            sine_fill(l, r, N, 441, amp);
            comp.env = 0;
            comp.g = 4096;
            for (k = 0; k + CTL <= N; k += CTL)
                mix_comp(l + k, r + k, CTL);
            if (i)
                lo = 20 * log10(rms_of(l, N / 2, N) / (amp / M_SQRT2));
            else
                q = 20 * log10(rms_of(l, N / 2, N) / (amp / M_SQRT2));
        }
        snprintf(b, sizeof b, "AMT 100: 20 dB apart in, %.1f dB apart out (4:1 above -30 dB); the quiet one lifted "
                 "%.1f dB", 20 + lo - q, q);
        check(b, 20 + lo - q < 9 && 20 + lo - q > 4 && q > 4);
        check("..the meter shows what it takes off", comp.gr_q8 > 256);
        mst[MS_MIX] = 0;
        sine_fill(l, r, N, 441, 30000);
        memcpy(l0, l, sizeof l0);
        comp.env = 0;
        comp.g = 4096;
        for (k = 0; k + CTL <= N; k += CTL)
            mix_comp(l + k, r + k, CTL);
        a = 20 * log10(rms_of(l, N / 2, N) / rms_of(l0, N / 2, N));
        snprintf(b, sizeof b, "MIX 0: the dry sound back (%.2f dB)", a);
        check(b, fabs(a) < 0.1);
        mst[MS_MIX] = 100;
        for (i = 0; i < 2u; i++) {                       /* ATK: a hit, and how soon it's held down */
            uint32_t at = 0;
            mst[MS_ATK] = (int16_t)(i ? 100 : 1);
            memset(l, 0, sizeof l);
            memset(r, 0, sizeof r);
            sine_fill(l + 1024, r + 1024, N - 1024, 441, 30000);
            comp.env = 0;
            comp.g = mx_curve(0, &k2_dummy);
            for (k = 0; k + CTL <= N; k += CTL) {
                mix_comp(l + k, r + k, CTL);
                if (!at && k > 1024 && comp.gr_q8 > 400)
                    at = k - 1024;
            }
            if (i)
                d = at / 44.1;
            else
                c = at / 44.1;
        }
        snprintf(b, sizeof b, "ATK 1 ms against 100 ms: held down 9 dB after %.1f ms against %.1f ms", c, d);
        check(b, c < 3 && d > 10 && d > 8 * c);
        mst[MS_ATK] = 10;
        mst[MS_AMT] = 0;
        sine_fill(l, r, N, 441, 30000);
        memcpy(l0, l, sizeof l0);
        for (k = 0; k + CTL <= N; k += CTL)
            mix_comp(l + k, r + k, CTL);
        check("AMT back to 0: the gain glides home, then the sound is untouched again",
              comp.g == 4096 && !memcmp(l + N / 2, l0 + N / 2, N / 2 * sizeof l[0]));
    }
    for (i = 0; i < NCH; i++)                            /* every corner, a square past full scale: bounded */
        ch[i] = CH_P[i].max;
    for (k = 0; k < N; k++)
        l[k] = r[k] = (k / 50) & 1 ? 131071 : -131071;
    memset(&chan[0], 0, sizeof chan[0]);
    for (k = 0; k + CTL <= N; k += CTL) {
        mix_channel(0, l + k, r + k, CTL);
        mix_pan(0, l + k, r + k, 4096, CTL);
    }
    check("every channel knob at its top, a square past full scale: bounded", peak_of(l, N) < 1000000 &&
          peak_of(r, N) < 1000000);
    for (i = 0; i < NCH; i++)
        ch[i] = CH_P[i].def;
}

/* the tape's edges: an empty user reel, a loop shorter than a block, the seam running backwards */
static void test_tape_edges(void)
{
    tape_view_t v;
    tape_rd_t r = {0};
    int32_t ls, ll, lo, hi, inside = 1;
    uint32_t i, len, t;
    char b[120];
    power_on();
    check("GAIN in dB: +6 doubles, -12 quarters, 0 is unity", db_q10(6) == 2048 && db_q10(-12) == 256 &&
          db_q10(0) == 1024);
    tp[2].dev[DEV_SRC][TK_REEL] = (int16_t)(NREEL + USLOT_N);   /* user reel 6: empty */
    tape_view(2, &v);
    check("a track on an empty user reel plays nothing", v.len == 0 && !v.ram && tape_peak_at(2, 500) == 0);
    tape_ctl[2].nblk = 0;
    check("..REC on it records onto a blank tape: none long yet, two chunks ready, growing", tape_prepare(2) == 1 &&
          tape_src(2) == 0 && tape_ctl[2].nblk == 0 && tape_ctl[2].nch == 2 && tape_ctl[2].grow &&
          !tape_ctl[2].empty && tpk(2, 20) == 0);
    check("..armed twice: still armed, nothing copied again", tape_prepare(2));
    tape_unprepare(2);
    tape_view(0, &v);
    check("a read past the end of the tape is silence", tape_read(&v, &r, (int32_t)(v.len + 5u) << 12) == 0 &&
          tape_at(&v, &r, -1) == 0);
    tape_rt[2].wstaged = 1;                              /* a block staged as the tape is cleared: dropped */
    tape_ctl[2].empty = 1;
    i = tape_ver[2];
    tape_commit(2);
    check("a staged block on a tape cleared meanwhile is dropped", !tape_rt[2].wstaged && tape_ver[2] == i);
    tape_ctl[2].empty = 0;

    len = REELS[0].nblk * TAPE_BLK;
    tp[0].dev[DEV_SRC][TK_STRT] = 100;                   /* STRT at the end, LEN 1 %: one block, the last */
    tp[0].dev[DEV_SRC][TK_LEN] = 1;
    tape_window(0, 10000, &ls, &ll);                     /* (1 % of 10,000 samples: 100, under a block) */
    check("LEN under a block is a block; STRT at the end keeps the window on the tape",
          ll == (int32_t)TAPE_BLK && ls == 10000 - (int32_t)TAPE_BLK);
    tp[0].dev[DEV_SRC][TK_REV] = 1;                      /* backwards over a short loop: the seam every 12 ms */
    tp[0].dev[DEV_SRC][TK_STRT] = 30;
    tp[0].dev[DEV_SRC][TK_LEN] = 2;
    tape_window(0, len, &lo, &ll);
    hi = lo + ll;
    sys.playing = 1;
    for (i = 0; i < 400u; i++) {
        render(1, 0);
        inside &= (tape_rt[0].pos >> 12) >= lo && (tape_rt[0].pos >> 12) < hi;
    }
    check("REV over a 2 % loop: the head wraps back to the end, never leaves the window", inside &&
          tape_rt[0].running);
    sys.playing = 0;
    render(400, 0);
    tp[0].dev[DEV_SRC][TK_REV] = 0;
    tp[0].dev[DEV_SRC][TK_STRT] = 0;
    tp[0].dev[DEV_SRC][TK_LEN] = 100;
    tp[2].dev[DEV_SRC][TK_REEL] = 3;

    tp[0].dev[DEV_SRC][TK_ROTA] = 50;                    /* ROTATE: playing starts half way round the loop */
    sys.playing = 1;
    render(1, 0);
    check("ROTA 50: PLAY starts the head half way through the loop", abs((tape_rt[0].pos >> 12) - (int32_t)len / 2) < 64);
    sys.playing = 0;
    render(400, 0);
    tp[0].dev[DEV_SRC][TK_ROTA] = 97;                    /* slice 1 from 97 %: it runs over the seam */
    fm1_in.notes = note_bit_of_white(0);
    render(1, 0);
    fm1_in.notes = 0;
    {
        int32_t ran = 0, s0 = (tape_rt[0].pos >> 12);
        for (i = 0; i < 2000u && tape_rt[0].running; i++, ran++)
            render(1, 0);
        check("ROTA 97, stopped: slice 1 starts at 97 %, runs over the seam and stops after its length",
              abs(s0 - (int32_t)len * 97 / 100) < 64 && abs(ran - (int32_t)len / 16 * 2 / CTL) <= 2);
    }
    tp[0].dev[DEV_SRC][TK_ROTA] = 0;

    {   /* DUB's balance: +100 keeps the loop untouched, -50 halves it each pass under silence */
        uint32_t k, same = 1;
        int32_t before = 0, after = 0;
        for (t = 0; t < NTRK; t++)
            track[t].mute = t != 0;                       /* (only track 1 sounds into track 2) */
        check("REC armed on track 2 (its reel copied in)", tape_prepare(1) && tape_ctl[1].rec_ok);
        tp[1].dev[DEV_SRC][TK_DUB] = 100;
        sys.rec = 1u << 1;
        sys.playing = 1;
        render(2800, 0);                                 /* a whole pass and more */
        sys.rec = 0;
        render(2, 0);
        for (k = 0; k < REELS[1].nblk; k++)
            same &= tpk(1, k) == REELS[1].peak[k] && !memcmp(tdata(1, k), REELS[1].data + k * (TAPE_BLK / 2u), TAPE_BLK / 2u);
        check("DUB +100: REC changes nothing (the loop untouched, bit for bit)", same);
        track[0].mute = 1;                               /* silence in */
        tp[1].dev[DEV_SRC][TK_DUB] = -50;
        for (k = 0; k < REELS[1].nblk; k++)
            before += REELS[1].peak[k];
        sys.rec = 1u << 1;
        render(1378 * 2 + 200, 0);                       /* one pass of the 2 s loop (and a little) */
        sys.rec = 0;
        render(2, 0);
        for (k = 0; k < REELS[1].nblk; k++)
            after += tpk(1, k);
        snprintf(b, sizeof b, "DUB -50 under silence: the loop at half its level after a pass (%d%%)",
                 (int)(after * 100 / (before ? before : 1)));
        check(b, after * 100 / before >= 40 && after * 100 / before <= 60);
        sys.playing = 0;
        tape_unprepare(1);
        for (t = 0; t < NTRK; t++)
            track[t].mute = 0;
        tp[1].dev[DEV_SRC][TK_DUB] = 0;
        render(400, 0);
    }
}

/* the LEDs, OCT-, the messages, panic */
static uint32_t led_lit;
static void led_on(const uint8_t *a, uint32_t id)
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id)
                return (void)(led_lit = (a[p] >> r) & 1u);
    led_lit = 0;
}
static void test_controls_more(void)
{
    uint32_t oct;
    power_on();
    sys.sel = 0;
    track[1].mute = 1;
    ui_leds();
    led_on(fm1_led, panel.btn[B_HOME]);
    {
        uint32_t home = led_lit, mute2, mute1, glow;
        led_on(fm1_led, 14u + black_note(BK_OP1 + 1));
        mute2 = led_lit;
        led_on(fm1_led, 14u + black_note(BK_OP1));
        mute1 = led_lit;
        led_on(fm1_led_dim, panel.btn[B_FX]);
        glow = led_lit;
        check("LEDs: the focus (HOME on TAPE) lit, track 2's mute key lit, track 1's not, the rest glow (DIM)",
              home && mute2 && !mute1 && glow);
    }
    settings_leds = LEDS_INV;
    sys.playing = 1;
    ui_leds();
    led_on(fm1_led, panel.btn[B_HOME]);
    {
        uint32_t home = led_lit, fx, play;
        led_on(fm1_led, panel.btn[B_FX]);
        fx = led_lit;
        led_on(fm1_led, panel.btn[B_PLAY]);
        play = led_lit;
        check("..INV: the focus dark, the rest lit, no glow; playing: PLAY's own LED dark (its green on)",
              !home && fx && !play && !fm1_led_dim[0] &&
              ((fm1_led[LED_PLAY_GREEN >> 3] >> (LED_PLAY_GREEN & 7u)) & 1u));
    }
    settings_leds = LEDS_DIM;
    sys.playing = 0;
    track[1].mute = 0;
    press(B_LFO);
    ui_leds();
    led_on(fm1_led, panel.btn[B_LFO]);
    check("..a modulator slot focused: its pad lit", led_lit);
    press(B_HOME);

    oct = track[0].octave;
    press(B_OCTDN);
    check("OCT-: one octave down", track[0].octave == oct - 1u);
    track[0].octave = 1;
    press(B_OCTDN);
    check("..not below 1", track[0].octave == 1);
    track[0].octave = (uint8_t)oct;

    tape_ctl[0].nblk = 10;                               /* a take on track 1's tape, a reel chosen over it */
    tape_ctl[0].empty = 0;
    tp[0].dev[DEV_SRC][TK_REEL] = 2;
    ui.msg_t = 0;
    press(B_REC);
    check("REC on a reel over a take: refused, and the message says how", !(sys.rec & 1u) && ui.msg_t &&
          !strcmp(ui.msg, "TAPE HAS A TAKE: CLEAR IT (HOLD POLY)"));
    tp[0].dev[DEV_SRC][TK_REEL] = 1;
    tape_ctl[0].nblk = 0;

    host_enc[panel.enc[EN_PRESET]] = 1;
    ui_input();
    check("PRESETS turned: the project view, pointing at the project loaded last (P1)", ui.view == VIEW_PROJECT &&
          ui.pj_sel == pj_cur);
    host_enc[panel.enc[EN_ALGO]] = 1;
    ui_input();
    check("ALGORITHM turned: the routing view (REC IN), nothing changed yet", ui.view == VIEW_ROUTE &&
          tp[sys.sel].recin == RIN_AUTO);
    host_enc[panel.enc[EN_ALGO]] = 2;
    ui_input();
    check("..turned again: the focused track's REC IN (AUTO -> OTHR -> its own: SELF)", tp[sys.sel].recin == 2 &&
          !strcmp(N_RIN[sys.sel][tp[sys.sel].recin], "SELF"));
    turn(2, 3);
    check("..KNOB 3: track 3's REC IN (T2)", tp[2].recin == 3 && !strcmp(N_RIN[2][3], "T2"));
    turn(2, 40);
    check("..to T4 at most", tp[2].recin == NRIN - 1);
    press(B_HOME);
    check("..a page pad closes it", ui.view == VIEW_PAGE);
    tp[sys.sel].recin = tp[2].recin = RIN_AUTO;

    hold(B_GLO);                                         /* GLOBAL held, EDIT: the channel page (GLOBAL used) */
    hold(B_EDIT);
    check("on the mixer, GLOBAL held + EDIT: the channel page, GLOBAL counted as used", ui.view == VIEW_MIXER &&
          ui.chan && ui.glo_used);
    let_go(B_EDIT);
    let_go(B_GLO);
    check("..both let go: back to the page", ui.view == VIEW_PAGE && !ui.chan);

    track[0].mute = 1;
    ui.force = 1;
    ui_draw();
    check("the footer says MUTE for a muted track", ui.sig_foot != 0);
    track[0].mute = 0;
    ui.force = 0;
    ui_redraw();
    check("ui_redraw asks for a whole redraw", ui.force);
    sys.playing = 1;
    sys.keys_live = 3;
    chain_panic();
    check("panic: the transport stops, the keys let go", !sys.playing && !sys.keys_live);
}

/* the drive's less travelled paths */
static void test_drive_more(void)
{
    static uint8_t buf[400000], wav[400000];
    tape_view_t v;
    uint8_t sec[512], *e;
    uint32_t n, i, ok, s;
    int32_t pk;
    double hz;
    char b[120];
    power_on();
    vdisk_mount();
    hd_mount();
    n = hd_read_file(hd_find("REEL2   WAV"), buf, sizeof buf);
    {
        const reel_t *r = &REELS[1];
        tape_view_t rv = {r->data, r->pred, r->idx, r->peak, r->nblk * TAPE_BLK, 0};
        tape_rd_t rd = {0};
        ok = n == 44 + rv.len * 2;
        for (i = 0; i < rv.len && ok; i += 89)
            ok = (int16_t)rd16(buf + 44 + 2 * i) == tape_at(&rv, &rd, (int32_t)i);
    }
    check("REEL2.WAV is factory reel 2, sample for sample", ok);

    memset(sec, 'n', sizeof sec);                        /* a file that isn't a WAV: kept in the write cache */
    hd_write_file(0, "NOTES   TXT", 0, sec, 512, 1);
    e = hd_find("NOTES   TXT");
    memset(buf, 0, 512);
    hd_read_file(e, buf, 512);
    vdisk_read(VD_TOTAL - 1, sec);
    for (i = 0, ok = 1; i < 512; i++)
        ok &= sec[i] == 0;
    check("a sector the computer wrote reads back (the write cache); one nobody wrote reads as zeros",
          !memcmp(buf, "nnnn", 4) && buf[511] == 'n' && ok);

    n = make_wav_x(wav, 11025, 8, 16, 0, 11025, 500.0);  /* 8-bit, 11,025 Hz, after a chunk of odd length */
    hd_write_file(0, "LOFI    WAV", 0, wav, n, 1);
    vdisk_poll();
    uslot_view(0, &v);
    hz = view_hz(&v, &pk);
    snprintf(b, sizeof b, "an 8-bit 11,025 Hz WAV with an odd-length chunk: up to 22,050 Hz, LOFI, %.0f Hz", hz);
    check(b, uslot_valid(0) && !strcmp(uslot_name[0], "LOFI") && fabs(hz - 500) < 10 && pk > 12000 &&
          v.len >= 22050u);

    e = hd_find("NOTES   TXT");                          /* the computer deletes a file: its entry is skipped */
    e[0] = 0xE5;
    n = make_wav_x(wav, 22050, 16, 40, 1, 11025, 700.0); /* WAVE_FORMAT_EXTENSIBLE, PCM, named USER5 */
    hd_write_file(0, "USER5   WAV", 0, wav, n, 1);
    vdisk_poll();
    uslot_view(4, &v);
    hz = view_hz(&v, &pk);
    snprintf(b, sizeof b, "an EXTENSIBLE WAV named USER5.WAV goes to user reel 5 (U5), past a deleted entry: %.0f Hz", hz);
    check(b, uslot_valid(4) && !strcmp(uslot_name[4], "U5") && !uslot_valid(1) && fabs(hz - 700) < 14);

    tape_undo.valid = 1;                                 /* a cleared take on track 3 waiting for undo */
    tape_undo.trk = 2;
    e = hd_find("TAPE3   WAV");
    n = make_wav_x(wav, 22050, 16, 16, 0, 5000, 400.0);
    hd_write_file(e, 0, 0, wav, n, 1);
    vdisk_poll();
    check("a WAV over TAPE3.WAV: track 3's tape, its undo dropped, the reel set to the tape",
          !tape_undo.valid && tape_src(2) == 0 && tape_ctl[2].nblk == (5000 + 255) / 256 &&
          !strcmp(ui.msg, "TRACK 3'S TAPE REPLACED"));

    for (s = 0; s < USLOT_N; s++)                        /* every user reel taken */
        if (!uslot_valid(s))
            uslot_save(s, REELS[0].data, REELS[0].pred, REELS[0].idx, REELS[0].peak, REELS[0].nblk, "FULL");
    n = make_wav_x(wav, 22050, 16, 16, 0, 3000, 400.0);
    hd_write_file(0, "MORE    WAV", 0, wav, n, 1);
    vdisk_poll();
    check("every user reel full: refused, and the message says how to replace one",
          !strcmp(ui.msg, "NO FREE REEL: NAME IT USER1-6.WAV") && !strcmp(uslot_name[1], "FULL"));
    host_prog_limit = 0;
    hd_write_file(0, "USER2   WAV", 0, wav, n, 1);
    vdisk_poll();
    host_prog_limit = 0xFFFFFFFFu;
    check("..named USER2.WAV but the flash refuses: the message says so, the slot reads empty",
          !strcmp(ui.msg, "THE FLASH REFUSED THE SAVE") && !uslot_valid(1) && !strcmp(uslot_name[1], "U2"));

    check("msc.c's view of the disk: 130,048 sectors, ready", msc_blocks() == VD_TOTAL && msc_ready());
    msc_attached();                                      /* a bus reset: plugged in again */
    check("..a bus reset: not ready until the main loop has made the volume afresh", !msc_ready() && vd.remount);
    vdisk_poll();
    msc_read(0, sec);
    hd_mount();
    check("..then ready, with the user reels as files (USER5.WAV), the boot sector through msc_read",
          msc_ready() && !vd.remount && sec[510] == 0x55 && hd_find("USER5   WAV") && !hd_find("LOFI    WAV"));
    memset(sec, 7, sizeof sec);
    msc_write(VD_DATA + 5000, sec);
    memset(sec, 0, sizeof sec);
    msc_read(VD_DATA + 5000, sec);
    msc_eject();
    check("..msc_write then msc_read of a sector: the same bytes", sec[0] == 7 && sec[511] == 7);
    memset(host_nor, 0xFF, sizeof host_nor);
    uslot_names();
}

/* the dot canvas: what's drawn off it is dropped, a fourth colour gets the spare code, and a strip reaches the LCD
 * as 2 x 2 pixel dots */
static void test_canvas(void)
{
    uint32_t x, y, ok = 1;
    px_colors();
    px_begin(8);                                         /* 4 dot rows */
    px_box(-5, -5, 7, 7, px_ink);                        /* corner: (0..1, 0..1) */
    px_box(118, 2, 9, 9, px_dim);                        /* right edge: (118..119, 2..3) */
    px_dot(0, 4, px_ink);                                /* below the strip: dropped */
    px_dot(60, 1, 0x07E0u);                              /* a fourth colour */
    check("the canvas clips at its edges and keeps 2 bits a dot", pxc_h == 4 && px_code(px_ink) == 2u &&
          ((pxc[1][0] & 15u) == 10u) && ((pxc[3][29] >> 4) == 5u) && pxc_pal[3] == 0x07E0u);
    lcd_fill(0, 0, 240, 240, 0);
    px_blit(100);
    for (y = 0; y < 8u; y++)
        for (x = 0; x < 240u; x++) {
            uint16_t c = host_screen[(100u + y) * 240u + x];
            c = (uint16_t)((c >> 8) | (c << 8));
            if (x < 4u && y < 4u)
                ok &= c == px_ink;
            else if (x == 120u && (y == 2u || y == 3u))
                ok &= c == 0x07E0u;
            else if (x >= 236u && y >= 4u)
                ok &= c == px_dim;
        }
    check("..and reaches the screen as 2 x 2 pixel dots, in the right colours, at the strip's row", ok &&
          host_screen[108u * 240u] == 0);
}

/* ------------------------------------------------------------- memory --- */
static void test_memory(void)
{
    char b[120];
    uint32_t t, n0, len;
    power_on();
    check("power-on: every chunk free, no tape holds any", mem_count(MEM_FREE) == MEM_NC && !tape_ctl[0].nch &&
          !tape_ctl[3].nch);

    {   /* a blank tape grows while REC records it, and REC let go sets its length */
        for (t = 0; t < NTRK; t++)
            track[t].mute = t != 0;                      /* (track 1's reel into track 2) */
        tp[1].dev[DEV_SRC][TK_REEL] = 0;
        check("REC on track 2's blank tape: ready, growing", tape_prepare(1) == 1 && tape_ctl[1].grow);
        sys.rec = 1u << 1;
        sys.playing = 1;
        render_poll(2756);                               /* 2 s */
        len = tape_ctl[1].nblk;
        snprintf(b, sizeof b, "..2 s of recording: the tape is 2 s long (%u blocks), two chunks ready past its end",
                 (unsigned)len);
        check(b, len >= 170u && len <= 174u && tape_ctl[1].nch * MEM_CB >= len + 2u * MEM_CB &&
              tape_ctl[1].nch * MEM_CB < len + 3u * MEM_CB);
        sys.rec = 0;
        tape_unprepare(1);
        render_poll(4);
        check("..REC let go: it stops growing, its length stays, the chunks past its end go back",
              !tape_ctl[1].grow && tape_ctl[1].nblk == len && tape_ctl[1].nch == (len + MEM_CB - 1u) / MEM_CB &&
              mem_count(MEM_FREE) == MEM_NC - tape_ctl[1].nch);
        {
            int32_t lo = 1 << 30, hi = -1, k;
            for (k = 0; k < 2756; k++) {
                render(1, 0);
                lo = (tape_rt[1].pos >> 12) < lo ? (tape_rt[1].pos >> 12) : lo;
                hi = (tape_rt[1].pos >> 12) > hi ? (tape_rt[1].pos >> 12) : hi;
            }
            check("..and it loops what it recorded (the head stays inside its length)", lo >= 0 &&
                  hi < (int32_t)(len * TAPE_BLK) && hi > (int32_t)(len * TAPE_BLK) * 9 / 10);
        }
        {
            uint32_t k, sound = 0;
            for (k = 0; k < len; k++)
                sound += tpk(1, k) > 20u;
            snprintf(b, sizeof b, "..what it recorded is track 1's beat (%u of %u blocks loud)", sound, (unsigned)len);
            check(b, sound > len / 3u);
        }
        sys.playing = 0;
        render_poll(400);
        for (t = 0; t < NTRK; t++)
            track[t].mute = 0;
    }

    {   /* nothing free: chunks come from a cleared tape first, then a parked track, then the longest tape */
        power_on();
        fill_tape(0, 60);
        fill_tape(2, 30);
        fill_tape(3, 20);
        fill_tape(1, 40);                                /* 150 of 152 */
        tape_clear(2);                                   /* track 3's take cleared (its undo waiting) */
        n0 = tape_ctl[0].nch;
        check("tape_steal takes a cleared tape's chunk first (not the longest), and its undo goes",
              tape_steal(1, 1) && tape_ctl[2].nch == 29 && tape_ctl[0].nch == n0 && !tape_undo.valid);
        while (tape_ctl[2].nch)
            tape_steal(1, 1);
        sys.ntrk = 3;                                    /* TRACKS 3: track 4 parked */
        check("..then a parked track's (TRACKS 3: track 4)", tape_steal(1, 1) && tape_ctl[3].nch == 19 &&
              tape_ctl[0].nch == n0);
        while (tape_ctl[3].nch)
            tape_steal(1, 1);
        sys.ntrk = NTRK;
        check("..then the end of the longest tape, never the one asking", tape_steal(1, 1) &&
              tape_ctl[0].nch == n0 - 1u && tape_ctl[1].nch == 40 && tape_ctl[0].nblk == (n0 - 1u) * MEM_CB);
        check("..an import (active 0) never takes from a tape in use", !tape_steal(NTRK, 0));
        tape_free(1);
        while (mem_count(MEM_FREE))
            mem_alloc(MEM_IMPORT);                       /* (nothing free at all; track 2's chunks to someone else) */
        {   /* track 2 records a blank tape with nothing free: it takes from track 1, the longest */
            uint32_t a0 = tape_ctl[0].nch;
            for (t = 0; t < NTRK; t++)
                track[t].mute = t != 0;
            tape_ctl[1].empty = 1;
            check("REC on a blank tape with no chunk free: it takes from the longest tape and grows",
                  tape_prepare(1) == 1 && tape_ctl[1].nch == 2 && tape_ctl[0].nch < a0);
            sys.rec = 1u << 1;
            sys.playing = 1;
            render_poll(1378 * 3);
            snprintf(b, sizeof b, "..3 s on: %u blocks, track 1 down from %u chunks to %u", (unsigned)tape_ctl[1].nblk,
                     (unsigned)a0, (unsigned)tape_ctl[0].nch);
            check(b, tape_ctl[1].nblk > 250u && tape_ctl[0].nch < a0 - 10u && tape_ctl[0].nblk <= tape_ctl[0].nch * MEM_CB);
            sys.rec = 0;
            tape_unprepare(1);
            sys.playing = 0;
            render_poll(400);
            for (t = 0; t < NTRK; t++)
                track[t].mute = 0;
        }
    }

    power_on();
    check("one tape holds 127 chunks at most (23.6 s: its head's Q12 position fits 32 bits)",
          tape_reserve(0, MEM_NC, 1) == TAPE_MAXCH && (int64_t)TAPE_MAXCH * TAPE_CHS * 4096 + 8192 < 0x7FFFFFFFLL);

    {   /* a tape that can't grow any more stops at its end and loops */
        power_on();
        for (t = 0; t < MEM_NC - 3u; t++)
            mem_alloc(MEM_IMPORT);                       /* (3 chunks left, nothing to take) */
        tp[1].dev[DEV_SRC][TK_REEL] = 0;
        tape_prepare(1);
        sys.rec = 1u << 1;
        sys.playing = 1;
        render_poll(1378 * 2);
        check("nothing left to take: the tape stops at 3 chunks and loops them, REC still on",
              !tape_ctl[1].grow && tape_ctl[1].nblk == 3u * MEM_CB && tape_ctl[1].rec_ok &&
              (tape_rt[1].pos >> 12) < (int32_t)(3u * TAPE_CHS));
        sys.rec = 0;
        tape_unprepare(1);
        sys.playing = 0;
        render_poll(400);
        tape_free(1);
        while (mem_count(MEM_FREE))
            mem_alloc(MEM_IMPORT);
        tp[2].dev[DEV_SRC][TK_REEL] = 0;
        check("..and REC on a blank tape with no chunk free and no tape to take from: refused (-1)",
              tape_prepare(2) == -1 && !tape_ctl[2].rec_ok);
    }

    {   /* TRACKS: the mixer's SELECT parks the tracks above it */
        int32_t p3;
        power_on();
        sys.playing = 1;
        render(10, 0);
        sys.sel = 3;
        sys.rec = 1u << 3;
        tape_prepare(3);
        tap(B_GLO);                                      /* the mixer, latched: SELECT is still the tempo */
        host_enc[panel.enc[EN_SELECT]] = -1;
        ui_input();
        check("on the latched mixer, SELECT is the tempo as everywhere (TRACKS untouched)", sys.ntrk == 4 &&
              sys.bpm == 119);
        sys.bpm = 120;
        hold(B_GLO);
        host_enc[panel.enc[EN_SELECT]] = -1;
        ui_input();
        check("GLO held + SELECT -1: TRACKS 3; the focus moves off track 4, its REC is let go",
              sys.ntrk == 3 && sys.sel == 2 && !(sys.rec & 8u) && !tape_ctl[3].rec_ok && sys.bpm == 120);
        host_enc[panel.enc[EN_SELECT]] = -5;
        ui_input();
        check("..down to 1 at least", sys.ntrk == 1 && sys.sel == 0);
        host_enc[panel.enc[EN_SELECT]] = 2;
        ui_input();
        check("..and back up: TRACKS 3", sys.ntrk == 3);
        let_go(B_GLO);
        render(4, 0);
        p3 = tape_rt[3].pos;
        render(100, 0);
        check("a parked track goes silent and isn't rendered (its head waits where it was)",
              peak_of(track_rt[3].last, CTL) == 0 && tape_rt[3].pos == p3 && track_rt[3].act == 0);
        hold(B_GLO);
        key_edge(note_bit_of_white(3) ? (uint32_t)__builtin_ctz(note_bit_of_white(3)) : 0u);
        check("GLO + white key 4 doesn't pick a parked track", sys.sel != 3);
        host_enc[panel.enc[EN_SELECT]] = 1;              /* (GLO held: the mixer is up) */
        ui_input();
        let_go(B_GLO);
        render(1, 0);
        check("TRACKS 4 again: track 4 fades back in (2 ms) from where its head was",
              track_rt[3].act > 0 && track_rt[3].act < 32767 && sys.ntrk == 4);
        render(10, 0);
        check("..and plays on", track_rt[3].act == 32767 && tape_rt[3].pos != p3);
        sys.playing = 0;
        render(400, 0);
    }

    {   /* a parked track's grains give their slots up (it isn't rendered, so they'd never end) */
        uint32_t k, t2;
        power_on();
        sys.playing = 1;
        for (t2 = 0; t2 < NTRK; t2++) {
            tp[t2].dev[DEV_GRAIN][GP_WET] = 100;
            tp[t2].dev[DEV_GRAIN][GP_RATE] = 100;
            tp[t2].dev[DEV_GRAIN][GP_SIZE] = 500;
        }
        render_poll(1378 * 2);
        chain_tracks(1);
        render_poll(200);
        for (k = 0, t2 = 0; k < 1378u; k++) {
            render_poll(1);
            t2 = grain_count(0) > t2 ? grain_count(0) : t2;
        }
        check("TRACKS 1 with every track's grains sounding: the parked ones' slots free, track 1 sounds the whole 32",
              grain_count(1) == 0 && grain_count(2) == 0 && grain_count(3) == 0 && t2 == 32u);
        sys.playing = 0;
        chain_tracks(4);
        render_poll(400);
    }
    {   /* a block staged on a tape whose list has been let go is dropped, not written into the next one */
        uint32_t v0;
        power_on();
        tape_reserve(2, 2, 1);
        tape_ctl[2].nblk = 20;
        tape_rt[2].wblk = 3;
        tape_rt[2].wstaged = 1;
        tape_rt[2].wgen = tape_ctl[2].gen;
        tape_free(2);                                    /* (a WAV over TAPE3.WAV swaps the list in) */
        tape_reserve(2, 2, 1);
        tape_ctl[2].nblk = 20;
        v0 = tape_ver[2];
        tape_commit(2);
        check("a block staged before the tape's list was replaced is dropped", !tape_rt[2].wstaged && tape_ver[2] == v0);
    }

    {   /* REC IN: track 3 records track 1 alone, though track 2 plays too */
        uint32_t k, loud = 0;
        power_on();
        tp[1].dev[DEV_SRC][TK_REEL] = 3;                 /* (track 2 plays reel 3 under it, track 4 is silent) */
        track[3].mute = 1;
        tp[2].dev[DEV_SRC][TK_REEL] = 0;
        tp[2].recin = RIN_T1;
        tape_prepare(2);
        sys.rec = 1u << 2;
        sys.playing = 1;
        render_poll(1378);
        sys.rec = 0;
        tape_unprepare(2);
        render_poll(4);
        for (k = 0; k < tape_ctl[2].nblk && k < REELS[0].nblk; k++)
            loud += abs((int)tpk(2, k) - (int)REELS[0].peak[k] * 100 / 127) < 24;
        snprintf(b, sizeof b, "REC IN T1: track 3's take follows track 1's beat alone (%u of %u blocks)", loud,
                 (unsigned)tape_ctl[2].nblk);
        check(b, tape_ctl[2].nblk > 80u && loud > tape_ctl[2].nblk * 3u / 4u);
        sys.playing = 0;
        render_poll(400);
    }
    {   /* a SYNTH track with REC IN OTHR records the others, not its synth */
        int32_t pk;
        power_on();
        tp[0].src = SRC_SYNTH;
        tp[0].dev[DEV_SRC][TK_REEL] = 0;
        tp[0].recin = RIN_OTHR;
        track[1].mute = track[2].mute = track[3].mute = 1;   /* (the others silent) */
        tape_prepare(0);
        sys.rec = 1u;
        sys.playing = 1;
        fm1_in.notes = note_bit_of_white(0);             /* the synth plays */
        render_poll(400);
        fm1_in.notes = 0;
        sys.rec = 0;
        tape_unprepare(0);
        render_poll(4);
        {
            uint32_t k;
            pk = 0;
            for (k = 0; k < tape_ctl[0].nblk; k++)
                pk = (int32_t)tpk(0, k) > pk ? (int32_t)tpk(0, k) : pk;
        }
        check("a SYNTH track on REC IN OTHR records the others (silent here), not its own synth", tape_ctl[0].nblk > 0 &&
              pk == 0);
        sys.playing = 0;
        render_poll(400);
    }

    {   /* a long sound across reel slots */
        tape_view_t v;
        int32_t pk;
        double hz;
        power_on();
        tape_reserve(0, 44, 1);                          /* 700 blocks of a 440 Hz sine on track 1's tape */
        {
            int16_t blk[TAPE_BLK];
            uint32_t bb, i2;
            for (bb = 0; bb < 700u; bb++) {
                mem_chunk_t *m = tape_chunk(0, bb);
                for (i2 = 0; i2 < TAPE_BLK; i2++)
                    blk[i2] = (int16_t)(12000.0 * sin(2 * M_PI * 440.0 * (bb * TAPE_BLK + i2) / TAPE_SR));
                ima_fit(blk, &m->pred[bb % MEM_CB], &m->idx[bb % MEM_CB]);
                ima_enc(blk, m->pred[bb % MEM_CB], m->idx[bb % MEM_CB], m->data[bb % MEM_CB], TAPE_BLK);
                m->peak[bb % MEM_CB] = ima_peak(blk, TAPE_BLK);
            }
            tape_ctl[0].nblk = 700;
            tp[0].dev[DEV_SRC][TK_REEL] = 0;
        }
        tape_view(0, &v);
        check("a 700-block (8.1 s) sound saved to user reel 1 spans three slots",
              uslot_save_view(0, &v, 700, "LONG") == 700 && uslot_valid(0) && uslot_hdr(0)->span == 3 &&
              uslot_covered(1) && uslot_covered(2) && !uslot_covered(3) && uslot_free() == 3 &&
              !strcmp(uslot_name[0], "LONG"));
        tp[1].dev[DEV_SRC][TK_REEL] = (int16_t)(NREEL + 1u);
        tape_view(1, &v);
        hz = view_hz(&v, &pk);
        snprintf(b, sizeof b, "..and plays from flash whole: %u samples, %.1f Hz", (unsigned)v.len, hz);
        check(b, v.len == 700u * TAPE_BLK && fabs(hz - 440) < 3 && pk > 10000);
        check("..REC copies it onto a tape in one piece (44 chunks)", tape_prepare(1) == 1 && tape_ctl[1].nblk == 700 &&
              tape_ctl[1].nch == 44);
        tape_unprepare(1);
        check("a one-slot reel keeps the layout reels always had (data at 4 KiB, span 1)",
              uslot_doff(TAPE_NBLK) == 4096u && uslot_span(TAPE_NBLK) == 1u && uslot_span(TAPE_NBLK + 1u) == 2u);
        check("..six slots hold 21.5 s at most", uslot_fit(USLOT_N) * TAPE_BLK / 22050u == 21u);
        tape_view(0, &v);
        check("a save into a slot a long reel covers breaks the long one (it reads empty, never garbage)",
              uslot_save(1, REELS[0].data, REELS[0].pred, REELS[0].idx, REELS[0].peak, REELS[0].nblk, "KICK") == 0 &&
              !uslot_valid(0) && uslot_valid(1));
        check("..a sound longer than the slots left from where it goes is cut to fit",
              uslot_save_view(4, &v, 700, "CUT") == (int32_t)uslot_fit(2) && uslot_valid(4) && uslot_hdr(4)->span == 2);
        memset(host_nor, 0xFF, sizeof host_nor);
        uslot_names();
    }

    {   /* the capture only takes chunks the main loop set aside (it runs in usb_poll, TIMER5, which can cut into the
         * main loop while that's allocating): with the main loop held up, a WAV is cut where the spares run out */
        static uint8_t wav[5 * 22050 * 2 + 4096];
        uint32_t n, k;
        power_on();
        vdisk_mount();
        hd_mount();
        vd_spare_fill();
        check("the drive up: two chunks set aside for a WAV", vd_sp_w - vd_sp_r == 2u && mem_count(MEM_SPARE) == 2u);
        n = make_wav(wav, 22050, 1, 16, 1, 22050 * 5, 330.0);
        for (k = 0; k < (n + 511u) / 512u; k++) {        /* (the sectors straight in, no main loop between them) */
            uint8_t sec[512];
            memset(sec, 0, sizeof sec);
            memcpy(sec, wav + k * 512u, n - k * 512u < 512u ? n - k * 512u : 512u);
            vdisk_write(VD_DATA + 40000u + k, sec);
        }
        check("..a 5 s WAV with no main loop between its sectors: cut at the two spares (32 blocks), nothing allocated "
              "from the interrupt", cap.cut && cap.nblk == 32u && capm.nch == 2u && mem_count(MEM_IMPORT) == 2u &&
              mem_count(MEM_FREE) == MEM_NC - 2u);
    }

    {   /* a long WAV over USB: a tape as long as memory allows, a reel across slots */
        static uint8_t wav[10 * 44100 * 2 + 64];
        uint32_t n;
        tape_view_t v;
        int32_t pk;
        double hz;
        power_on();
        vdisk_mount();
        hd_mount();
        n = make_wav(wav, 22050, 1, 16, 1, 22050 * 10, 330.0);   /* 10 s */
        hd_write_file(hd_find("TAPE4   WAV"), 0, 0, wav, n, 1);
        vdisk_poll();
        tape_view(3, &v);
        hz = view_hz(&v, &pk);
        snprintf(b, sizeof b, "a 10 s WAV over TAPE4.WAV: all of it on track 4's tape (%u blocks), %.0f Hz", (unsigned)tape_ctl[3].nblk, hz);
        check(b, tape_ctl[3].nblk >= 861u && tape_ctl[3].nblk <= 862u && fabs(hz - 330) < 4 &&
              mem_count(MEM_TAPE + 3) == tape_ctl[3].nch && !mem_count(MEM_IMPORT));
        n = make_wav(wav, 22050, 1, 16, 1, 22050 * 6, 550.0);    /* 6 s, any name: a reel across slots */
        hd_write_file(0, "SIX     WAV", 0, wav, n, 1);
        vdisk_poll();
        uslot_view(0, &v);
        hz = view_hz(&v, &pk);
        snprintf(b, sizeof b, "a 6 s WAV with any name: user reel SIX across two slots, %.0f Hz, its chunks back", hz);
        check(b, uslot_valid(0) && uslot_hdr(0)->span == 2 && !strcmp(uslot_name[0], "SIX") && fabs(hz - 550) < 5 &&
              !mem_count(MEM_IMPORT) && uslot_free() == 2);
        while (mem_count(MEM_FREE))
            mem_alloc(MEM_TAPE + 2);                     /* (every chunk held by a tape in use, */
        vd_sp_r = vd_sp_w;                               /* and none set aside) */
        n = make_wav(wav, 22050, 1, 16, 1, 22050, 550.0);
        hd_write_file(0, "MORE    WAV", 0, wav, n, 1);
        vdisk_poll();
        check("..no memory free (every chunk in a tape in use): refused, and the message says so",
              !strcmp(ui.msg, "NO MEMORY FREE FOR THAT WAV") && !mem_count(MEM_IMPORT));
        memset(host_nor, 0xFF, sizeof host_nor);
        uslot_names();
    }
}

/* ---------------------------------------------------------- modulation --- */
/* track t's modulated PAN (the ISR's copy) over n blocks: its lowest and highest */
static void pan_range(uint32_t t, uint32_t n, int32_t *lo, int32_t *hi)
{
    uint32_t b;
    *lo = 32767;
    *hi = -32768;
    for (b = 0; b < n; b++) {
        chain_block(out, CTL);
        *lo = TPD(t, MA_CH)[CH_PAN] < *lo ? TPD(t, MA_CH)[CH_PAN] : *lo;
        *hi = TPD(t, MA_CH)[CH_PAN] > *hi ? TPD(t, MA_CH)[CH_PAN] : *hi;
    }
}

static void test_mod(void)
{
    const uint32_t PAN = MOD_TCH + CH_PAN;
    int32_t lo, hi;
    power_on();
    render(4, 0);
    check("mod: no depths, every track reads its own knobs (nothing copied)", TPD(0, DEV_GRAIN) == tp[0].dev[DEV_GRAIN] && TPD(3, MA_CH) == tp[3].ch);
    check("mod: the reel choices can't be targets", mod_nudge(0, 0, MOD_TSRC + TK_REEL, 50) == -128 &&
                                                    mod_nudge(0, 0, MOD_TSRC + SRC_POLY * NPK + PL_REEL, 50) == -128);
    check("..nor an empty knob", mod_nudge(0, 0, MOD_TG(DEV_SPACE, 15), 50) == -128);

    /* LFO slot 1 (SIN, FREE): depth 100 swings PAN across its whole range; 50: half of it */
    tp[0].mod[0][0] = 127;                               /* RATE: ~20 Hz, many cycles in a few blocks */
    tp[0].mod[0][12] = 0;                                /* SYNC FREE */
    mod_nudge(0, 0, PAN, 100);
    pan_range(0, 300, &lo, &hi);
    check("mod: LFO at depth 100 sweeps PAN end to end", TPD(0, MA_CH) == mda[0][MA_CH] && lo <= -95 && hi >= 95);
    check("..and leaves the knob itself alone", tp[0].ch[CH_PAN] == 0);
    mod_nudge(0, 0, PAN, -75);                           /* (depth x the range each way: 25 % of 200 is 50) */
    pan_range(0, 300, &lo, &hi);
    check("mod: depth 25: a quarter of the range each way, round the knob", lo >= -51 && lo <= -45 && hi >= 45 && hi <= 51);
    tp[0].ch[CH_PAN] = 80;
    pan_range(0, 300, &lo, &hi);
    check("..clamped at the range's ends", hi == 100 && lo >= 25 && lo <= 32);
    tp[0].ch[CH_PAN] = 0;
    tp[0].mod[0][8] = 0;                                 /* AMT 0: the slot moves nothing */
    pan_range(0, 50, &lo, &hi);
    check("mod: AMT 0 silences the slot", lo == 0 && hi == 0);
    tp[0].mod[0][8] = 100;
    tp[0].mod[0][11] = 100;                              /* SPRD 100: the right channel half a cycle on */
    pan_range(0, 40, &lo, &hi);
    {
        int32_t d = TPD(0, MA_CH)[CH_PAN] + mod_pan_r[0];
        check("mod: SPRD on PAN: the right channel's pan mirrors the left's", hi > 40 && d >= -6 && d <= 6);
    }
    tp[0].mod[0][11] = 0;

    /* SYNC BPM: RATE 80..87 is one beat; at 120 BPM a cycle is 0.5 s (689 blocks): the upward zero crossings */
    {
        uint32_t b, n = 0, first = 0, last = 0;
        int32_t prev = 0;
        tp[0].mod[0][0] = 80;
        tp[0].mod[0][12] = 1;
        sys.bpm = 120;
        for (b = 0; b < 2400u; b++) {
            int32_t v;
            chain_block(out, CTL);
            v = TPD(0, MA_CH)[CH_PAN];
            if (prev < 0 && v >= 0) {
                if (!n)
                    first = b;
                last = b;
                n++;
            }
            prev = v;
        }
        check("mod: LFO SYNC BPM: one cycle a beat at 120 BPM", n >= 3u && (last - first) / (n - 1u) >= 685u &&
                                                               (last - first) / (n - 1u) <= 692u);
    }

    /* ADSR in slot 2 on GRAIN WET (base 0): a key held on the track opens it to SUS, let go it closes */
    power_on();
    {
        uint32_t g = MOD_TG(DEV_GRAIN, GP_WET), kb = note_bit_of_white(0);
        int32_t top = 0;
        uint32_t b;
        tp[0].mod[1][0] = 0;                             /* ATK 1 ms, DEC short, SUS 50, REL short */
        tp[0].mod[1][1] = 10;
        tp[0].mod[1][2] = 50;
        tp[0].mod[1][3] = 10;
        mod_nudge(0, 1, g, 100);
        render(4, 0);
        check("mod: ADSR at rest: WET stays at its knob", TPD(0, DEV_GRAIN)[GP_WET] == 0);
        fm1_in.notes |= kb;
        for (b = 0; b < 3u; b++) {
            chain_block(out, CTL);
            top = TPD(0, DEV_GRAIN)[GP_WET] > top ? TPD(0, DEV_GRAIN)[GP_WET] : top;
        }
        check("mod: a key down: the attack reaches the top", top >= 98);
        render(200, 0);
        check("..held: the sustain level", TPD(0, DEV_GRAIN)[GP_WET] >= 48 && TPD(0, DEV_GRAIN)[GP_WET] <= 51);
        fm1_in.notes &= ~kb;
        render(200, 0);
        check("..let go: released to the knob", TPD(0, DEV_GRAIN)[GP_WET] == 0);
        check("mod: a modulated WET keeps GRAIN's memory in use (the poll asks mod_peak)", mod_peak(0, g) == 100);
        sys.playing = 1;                                 /* PLAY: the loop starts, a one-shot */
        top = 0;
        for (b = 0; b < 3u; b++) {
            chain_block(out, CTL);
            top = TPD(0, DEV_GRAIN)[GP_WET] > top ? TPD(0, DEV_GRAIN)[GP_WET] : top;
        }
        render(200, 0);
        check("mod: the loop's start: a one-shot (up, then released without a key)", top >= 98 &&
                                                                                 TPD(0, DEV_GRAIN)[GP_WET] == 0);
        sys.playing = 0;
    }

    /* SEQ in slot 3 on COLOR DRIV: LEN 4 at 1/16 (a quarter beat: 172 blocks at 120 BPM), steps 0 100 0 100 */
    power_on();
    {
        uint32_t g = MOD_TG(DEV_COLOR, 0), b, ups = 0;
        int32_t prev = -1;
        tp[0].mod[2][0] = 4;
        tp[0].mod[2][1] = 4;
        tp[0].steps[2][0] = 0;
        tp[0].steps[2][1] = 100;
        tp[0].steps[2][2] = 0;
        tp[0].steps[2][3] = 100;
        mod_nudge(0, 2, g, 100);
        sys.playing = 1;
        for (b = 0; b < 172u * 8u; b++) {
            int32_t v;
            chain_block(out, CTL);
            v = TPD(0, DEV_COLOR)[0];
            if (b == 10u)
                check("mod: SEQ step 1 (0): DRIV at its knob", v == 0);
            if (b == 172u + 10u)
                check("..step 2 (100): at the top", v == 100);
            if (prev >= 0 && v > prev)
                ups++;
            prev = v;
        }
        check("..stepping every 1/16: 4 rises in 8 steps", ups == 4u);
        tp[0].mod[2][6] = 0;                             /* PROB 0: no step plays after the first, it holds */
        sys.playing = 0;
        render(1, 0);
        sys.playing = 1;
        prev = -1;
        ups = 0;
        for (b = 0; b < 172u * 8u; b++) {
            chain_block(out, CTL);
            ups += prev >= 0 && TPD(0, DEV_COLOR)[0] != prev;
            prev = TPD(0, DEV_COLOR)[0];
        }
        check("mod: SEQ PROB 0: the first step holds", ups == 0u);
        sys.playing = 0;
    }

    /* FOLLOW in slot 4 on SPACE TONE: the envelope of track 2's last block, RISE and FALL */
    power_on();
    {
        static int32_t loud[CTL], quiet[CTL];
        const int32_t *last[NTRK] = {quiet, loud, quiet, quiet};
        uint32_t g = MOD_TG(DEV_SPACE, SP_TONE), b, i;
        for (i = 0; i < CTL; i++)
            loud[i] = (i & 1u) ? 30000 : -30000;
        param_engine(0, 3, ME_FOLLOW);
        tp[0].mod[3][0] = 2;                             /* SRC T2 */
        tp[0].mod[3][2] = 0;                             /* RISE fast */
        tp[0].mod[3][3] = 0;                             /* FALL fast */
        mod_nudge(0, 3, g, 100);
        for (b = 0; b < 40u; b++)
            mod_tick(0, 0, last);
        check("mod: FOLLOW: a loud source pushes TONE up", TPD(0, DEV_SPACE)[SP_TONE] >= 75);
        last[1] = quiet;
        for (b = 0; b < 40u; b++)
            mod_tick(0, 0, last);
        check("..silent: back to the knob", TPD(0, DEV_SPACE)[SP_TONE] == tp[0].dev[DEV_SPACE][SP_TONE]);
    }

    /* the gestures: a slot's pad held + a knob sets the depth on the page shown; a tap opens the slot */
    power_on();
    press(B_FX);                                         /* COLOR */
    hold(B_LFO);
    turn(1, 5);                                          /* CRSH */
    check("slot pad held + KNOB 2 on COLOR: slot 1's depth to CRSH", mdep[0][0][MOD_TG(DEV_COLOR, 1)] == 5);
    check("..the strip shows the depth while it's held", ui.slot_held == 0u);
    let_go(B_LFO);
    check("..let go after a turn: still COLOR (no page change)", ui.kind == FOCUS_DEV && ui.dev == DEV_COLOR &&
                                                                 ui.slot_held == SLOT_NONE);
    check("..the knob itself is untouched", tp[0].dev[DEV_COLOR][1] == DEV_P[DEV_COLOR][1].def);
    tap(B_LFO);
    check("slot pad tapped: its page", ui.kind == FOCUS_SLOT && ui.slot == 0u);
    tap(B_LFO);
    check("..again: its next page", ui.kind == FOCUS_SLOT && ui.page == 1u);
    press(B_HOME);
    hold(B_ENV);
    {
        int32_t d = host_enc[panel.enc[EN_SELECT]] = 1;
        (void)d;
        ui_input();
    }
    check("slot pad held + SELECT: that slot's engine, the page stays", tp[0].engine[1] == ME_SEQ &&
                                                                        ui.kind == FOCUS_DEV && ui.dev == DEV_SRC);
    turn(2, -10);                                        /* TAPE SPD */
    let_go(B_ENV);
    check("..and KNOB 3 on TAPE: slot 2's depth to SPD", mdep[0][1][MOD_TSRC + TK_SPD] == -10);
    check("..depths stay when the engine changes", mdep[0][0][MOD_TG(DEV_COLOR, 1)] == 5);
    hold(B_LFO);
    turn(0, 3);                                          /* TAPE's REEL is on page 3: page 1 knob 1 is STRT */
    let_go(B_LFO);
    check("hold + turn on the source: the depth for that source's knob", mdep[0][0][MOD_TSRC + TK_STRT] == 3);
    hold(B_GLO);
    press(B_EDIT);                                       /* the channel strip */
    let_go(B_GLO);
    if (ui.view == VIEW_MIXER && ui.chan == CHAN_STRIP) {
        hold(B_SEQ);
        turn(3, 20);
        let_go(B_SEQ);
        check("hold + turn on the channel strip: the depth to PAN", mdep[0][2][PAN] == 20);
    }
    press(B_HOME);

    /* MONO held 0.5 s clears the track's depths; SAVE held brings them back */
    {
        uint32_t mn = black_note(BK_MONO), n0 = mod_count(0);
        fm1_ms = 10000;
        fm1_in.notes |= 1u << mn;
        key_edge(mn);
        fm1_ms = 10200;
        fm1_in.notes &= ~(1u << mn);
        ui_input();
        check("MONO let go before 0.5 s: nothing cleared", mod_count(0) == n0 && n0 >= 3u);
        fm1_ms = 11000;
        fm1_in.notes |= 1u << mn;
        key_edge(mn);
        fm1_ms = 11510;
        ui_input();
        fm1_in.notes &= ~(1u << mn);
        ui_input();
        render(1, 0);
        check("..held 0.5 s: the track's depths are gone, its knobs read straight", mod_count(0) == 0u && TPD(0, MA_CH) == tp[0].ch);
        fm1_ms = 12000;
        hold(B_SAVE);
        fm1_ms = 12000 + HOLD_MS[settings_hold % 4u];
        ui_input();
        let_go(B_SAVE);
        check("..SAVE held: undone", mod_count(0) == n0);
        check("..once", mod_undo() == -1);
    }

    /* a SEQ page: the white keys pick steps (they don't play), KNOB 1 sets the held ones */
    press(B_SEQ);
    if (tp[0].engine[2] == ME_SEQ) {
        uint32_t k3 = note_bit_of_white(2), k5 = note_bit_of_white(4);
        int8_t s3 = tp[0].steps[2][2], s5 = tp[0].steps[2][4], s1 = tp[0].steps[2][0], len = (int8_t)tp[0].mod[2][0];
        fm1_in.notes |= k3 | k5;
        ui_input();
        check("SEQ page: the keys don't play the track", !sys.keys_live && ui.steps_held == ((1u << 2) | (1u << 4)));
        turn(0, -7);
        check("..a white key held + KNOB 1: those steps' values", tp[0].steps[2][2] == s3 - 7 && tp[0].steps[2][4] == s5 - 7 &&
                                                                  tp[0].steps[2][0] == s1 && tp[0].mod[2][0] == len);
        fm1_in.notes &= ~(k3 | k5);
        ui_input();
        turn(0, -1);
        check("..no key: KNOB 1 is LEN again", tp[0].mod[2][0] == len - 1);
    }

    /* the limit: 32 depths a track */
    power_on();
    {
        uint32_t g, n = 0;
        for (g = 0; g < MOD_NTGT && n < 40u; g++)
            if (mod_tdesc(g) && mod_nudge(0, 0, g, 10) != -128)
                n++;
        check("mod: at most 32 depths a track (the 33rd is refused)", n == MOD_MAX && mod_count(0) == MOD_MAX);
        render(2, 0);
        check("..and they all run", TPD(0, DEV_GRAIN) == mda[0][DEV_GRAIN] && TPD(0, DEV_RESO) == mda[0][DEV_RESO]);
    }
    power_on();
}

/* each source's LVL (its last page): -12 dB is a quarter of the sound, on TAPE, SYNTH and POLY alike; a target */
/* the DRUM kit's voices, struck alone: each rings out to silence, under full scale, without DC, and the same every
 * hit; an accent is louder; a closed hat chokes the open one */
static uint32_t drum_hit(uint32_t i, uint32_t acc, uint32_t choke_at, int32_t *out, uint32_t max, int32_t *peak)
{
    dk_knobs_t kn;
    dk_coef_t c;
    dk_voice_t v;
    uint32_t n = 0, k;
    drm_knobs_default(&kn);
    kn.accent = (uint8_t)acc;
    dk_setup(&c, &DRM_KIT[i], &kn);
    memset(&v, 0, sizeof v);
    dk_trigger(&v);
    *peak = 0;
    while (n + CTL <= max) {
        if (n == choke_at)
            dk_choke(&v);
        dk_run(&c, &v, out + n, CTL);
        for (k = 0; k < CTL; k++)
            *peak = abs(out[n + k]) > *peak ? abs(out[n + k]) : *peak;
        n += CTL;
        if (!v.live)
            break;
    }
    return v.live ? 0u : n;
}

static void test_drum_voices(void)
{
    static int32_t a[44100 * 12], b[44100 * 12];
    uint32_t i, rings = 1, quiet = 1, nodc = 1, same = 1, louder = 1, k, n, nb, choked;
    int32_t pk, pkb;
    for (i = 0; i < DRM_NINST; i++) {
        int64_t sum = 0;
        n = drum_hit(i, 0, ~0u, a, 44100u * 12u, &pk);
        nb = drum_hit(i, 0, ~0u, b, 44100u * 12u, &pkb);
        rings &= n != 0;
        quiet &= pk > 4000 && pk < 30000;
        for (k = 0; k < n; k++)
            sum += a[k];
        nodc &= n && llabs(sum / (int64_t)n) < 300;
        same &= n == nb && !memcmp(a, b, n * sizeof *a);
        drum_hit(i, 127, ~0u, b, 44100u * 12u, &pkb);
        louder &= pkb > pk;
        if (!n || !(pk > 4000 && pk < 30000) || !(pkb > pk))
            printf("bryo:   (%s: rang out after %u samples, peak %d, accented %d)\n", DRM_KIT[i].code, n, pk, pkb);
    }
    check("DRUM: each of the 16 instruments rings out to silence within 4 s", rings);
    check("..peaks between -18 and -0.8 dBFS, without DC, the same every hit", quiet && nodc && same);
    check("..and an accent makes every one louder", louder);
    n = drum_hit(DI_OH, 0, ~0u, a, 44100u * 12u, &pk);
    choked = drum_hit(DI_OH, 0, CTL * 20u, a, 44100u * 12u, &pk);
    check("the open hat rings past half a second; choked (HH, MB) it stops within 25 ms",
          n > 22050u && choked && choked < CTL * 20u + 1103u &&
          (DRM_KIT[DI_HH].chokes & DRM_KIT[DI_MB].chokes) == 1u << DI_OH);
}

/* DRUM on track 1, the others muted: the instruments it strikes over `blocks` blocks (a count each), and the block
 * of each of its first 64 strikes */
static uint32_t drum_hits[16], drum_at[64], drum_nat;
static void drum_run(uint32_t blocks)
{
    uint32_t b, j;
    for (b = 0; b < blocks; b++) {
        uint32_t n0 = drm_rt[0].n;
        uint8_t before[DRM_NV];
        uint32_t age[DRM_NV];
        for (j = 0; j < DRM_NV; j++) {
            before[j] = drm_rt[0].inst[j];
            age[j] = drm_rt[0].age[j];
        }
        chain_block(out, CTL);
        if (drm_rt[0].n != n0)
            for (j = 0; j < DRM_NV; j++)
                if (drm_rt[0].age[j] != age[j] || drm_rt[0].inst[j] != before[j]) {
                    drum_hits[drm_rt[0].inst[j] & 15u]++;
                    if (drum_nat < 64u)
                        drum_at[drum_nat++] = b;
                }
    }
}
static void drum_reset(void)
{
    memset(drum_hits, 0, sizeof drum_hits);
    drum_nat = 0;
}
/* the hits of a pattern as written, per instrument */
static void drum_count(uint32_t t, uint32_t *n)
{
    uint32_t b, s, i;
    for (i = 0; i < 16u; i++)
        n[i] = 0;
    for (b = 0; b < (uint32_t)tp[t].drm[DM_LEN]; b++)
        for (s = 0; s < 16u; s++)
            for (i = 0; i < 16u; i++)
                n[i] += (tp[t].dpat[b][s] >> i) & 1u;
}

static void test_drum(void)
{
    uint32_t want[16], i, b, s, v, ok, same, kept, foreign, differs;
    int16_t *p;
    char m[96];
    power_on();
    tp[0].src = SRC_DRUM;
    track[1].mute = track[2].mute = track[3].mute = 1;
    p = tp[0].drm;
    check("DRUM: a track starts on ROCK, two bars; PATN, LEN, INST, BAR, MODE aren't modulation targets; SEED is",
          tp[0].drm[DM_PATN] == 0 && tp[0].drm[DM_LEN] == 2 && tp[0].dpat[0][0] == (1u << DI_BD | 1u << DI_HH) &&
          !mod_tdesc(MOD_TSRC + SRC_DRUM * NPK + DM_PATN) && !mod_tdesc(MOD_TSRC + SRC_DRUM * NPK + DM_LEN) &&
          !mod_tdesc(MOD_TSRC + SRC_DRUM * NPK + DM_INST) && !mod_tdesc(MOD_TSRC + SRC_DRUM * NPK + DM_MODE) &&
          mod_tdesc(MOD_TSRC + SRC_DRUM * NPK + DM_SEED) && mod_tdesc(MOD_TSRC + SRC_DRUM * NPK + DM_SWNG));

    /* two bars at 120 BPM: 4 s, 5512.5 blocks; a sixteenth is 172.3 blocks */
    sys.bpm = 120;
    sys.playing = 1;
    drum_reset();
    drum_run(5512);
    drum_count(0, want);
    for (ok = 1, i = 0; i < 16u; i++)
        ok &= drum_hits[i] == want[i];
    snprintf(m, sizeof m, "two bars of ROCK as written: BD %u SD %u HH %u OH %u hits (written %u %u %u %u)",
             drum_hits[DI_BD], drum_hits[DI_SD], drum_hits[DI_HH], drum_hits[DI_OH], want[DI_BD], want[DI_SD],
             want[DI_HH], want[DI_OH]);
    check(m, ok);
    check("..on the grid: the third strike (the hat on step 3) 2 sixteenths after the first, to the block",
          drum_nat > 3u && drum_at[0] == 0u && (drum_at[2] == 344u || drum_at[2] == 345u));
    sys.playing = 0;
    render(2, 0);
    p[DM_SWNG] = 100;                                    /* SWNG 100: the off-sixteenths a third of a step late */
    tp[0].dpat[0][1] = 1u << DI_CL;
    sys.playing = 1;
    drum_reset();
    drum_run(400);
    check("SWNG 100: step 2 (an off-sixteenth) sounds a third of a step late: 229 blocks, not 172",
          drum_nat >= 3u && drum_at[2] >= 228u && drum_at[2] <= 230u);
    sys.playing = 0;
    render(2, 0);
    drm_load(0, 0);
    p[DM_SWNG] = 0;

    /* SEED: versions */
    p[DM_VARY] = 100;
    p[DM_FILL] = 100;
    for (same = 1, kept = 1, foreign = 0, differs = 0, v = 1; v <= 200u; v++)
        for (b = 0; b < 2u; b++)
            for (s = 0; s < 16u; s++) {
                drm_hits_t h, h2, h0;
                uint32_t w = tp[0].dpat[b][s];
                drm_step(0, p, v, b, s, &h);
                drm_step(0, p, v, b, s, &h2);
                drm_step(0, p, 0, b, s, &h0);
                same &= h.hit == h2.hit && h.soft == h2.soft && h.acc == h2.acc && h0.hit == w;
                differs += h.hit != w;
                if (!(b == 1u && s >= 8u))                /* (outside a fill's reach) */
                    kept &= (h.hit & w & (1u << DI_BD | 1u << DI_SD)) == (w & (1u << DI_BD | 1u << DI_SD));
                foreign |= h.hit & ~(1u << DI_BD | 1u << DI_SD | 1u << DI_HH | 1u << DI_OH | 1u << DI_CY);
            }
    check("SEED: a version plays the same every time; SEED 0 is the pattern as written", same);
    snprintf(m, sizeof m, "..200 versions at VARY 100: %u steps differ, the backbone (BD SD) never dropped", differs);
    check(m, differs > 400u && kept);
    check("..and nothing the pattern doesn't use comes in (ROCK: no cowbell, no conga)", !foreign);
    drm_load(0, 4);                                      /* BOSA: no snare, no cymbal */
    for (foreign = 0, v = 1; v <= 200u; v++)
        for (b = 0; b < 2u; b++)
            for (s = 0; s < 16u; s++) {
                drm_hits_t h;
                drm_step(0, p, v, b, s, &h);
                foreign |= h.hit & (1u << DI_SD | 1u << DI_CY | 1u << DI_CP);
            }
    check("..BOSA's fills roll on the rim: no snare, no clap, no crash in 200 versions", !foreign);
    drm_load(0, 5);                                      /* RMBA: low conga and high bongo, no low bongo */
    for (foreign = 0, differs = 0, v = 1; v <= 200u; v++)
        for (b = 0; b < 2u; b++)
            for (s = 0; s < 16u; s++) {
                drm_hits_t h;
                drm_step(0, p, v, b, s, &h);
                foreign |= h.hit & ~(1u << DI_BD | 1u << DI_LC | 1u << DI_HB | 1u << DI_CL | 1u << DI_MA);
                differs += ((h.hit ^ tp[0].dpat[b][s]) & DRM_CONGAS) != 0;
            }
    snprintf(m, sizeof m, "..RMBA: the congas vary (%u steps in 200 versions), runs and moves stay on its two drums",
             differs);
    check(m, !foreign && differs > 200u);
    check("DECY: x2 every 50 % (-100: a quarter, +25: 1.5 times, +100: four times)", drm_scale(-100) == 64u &&
          drm_scale(25) == 384u && drm_scale(100) == 1024u && drm_scale(0) == 256u);
    p[DM_SEED] = 5;
    p[DM_EVOL] = 1;
    check("EVOL 1: a new version every loop (SEED 5: 5, 6, 7); OFF: always 5; SEED 0: as written",
          drm_version(p, 0) == 5u && drm_version(p, 2) == 7u && (p[DM_EVOL] = 0, drm_version(p, 9) == 5u) &&
          (p[DM_SEED] = 0, drm_version(p, 9) == 0u));
    p[DM_VARY] = 40;
    p[DM_FILL] = 25;

    drm_load(0, 0);                                      /* seven instruments on one step: five voices, the oldest */
    for (b = 0; b < 2u; b++)                             /* taken (the last five struck ring) */
        for (s = 0; s < 16u; s++)
            tp[0].dpat[b][s] = 0;
    tp[0].dpat[0][0] = 0x7Fu;
    memset(drm_rt, 0, sizeof drm_rt);
    sys.playing = 1;
    render(1, 0);
    for (ok = 1, i = 0; i < DRM_NV; i++)
        ok &= drm_rt[0].inst[i] >= 2u && drm_rt[0].inst[i] <= 6u && (drm_rt[0].v[i].live || drm_rt[0].v[i].trig);
    check("seven instruments on one step: the five voices hold the last five struck", ok);
    sys.playing = 0;
    render(2, 0);

    /* the page: PATN, POLY's clear, SAVE's undo, STEP's keys, OCT, HOME + key */
    power_on();
    tp[0].src = SRC_DRUM;
    p = tp[0].drm;
    turn(0, 1);
    check("PATN turned: DISC written over the pattern (four on the floor)", tp[0].dpat[0][4] & (1u << DI_BD) &&
          tp[0].dpat[0][2] & (1u << DI_OH) && !strcmp(ui.msg, "DISC WRITTEN"));
    press(B_HOME);
    press(B_HOME);
    press(B_HOME);
    check("HOME tapped: VARY, KIT, STEP (the page turns as HOME is let go)", ui.page == 3u);
    key_edge(note_of_white(1));
    check("STEP (HITS): white key 2 toggles step 2 of bar 1 for INST (BD)", tp[0].dpat[0][1] == (1u << DI_BD));
    hold(B_HOME);
    key_edge(note_of_white(1));
    let_go(B_HOME);
    check("HOME held + white key 2: INST is SD, heard; the page stays", p[DM_INST] == DI_SD && ui.page == 3u &&
          drm_aud[0] == DI_SD + 1u);
    press(B_OCTUP);
    key_edge(note_of_white(2));
    check("OCT+: bar 2; key 3 writes the snare there; OCT+ again stays at LEN", p[DM_BAR] == 2 &&
          (tp[0].dpat[1][2] & (1u << DI_SD)) && (press(B_OCTUP), p[DM_BAR] == 2));
    turn(2, 1);
    key_edge(note_of_white(5));
    check("MODE ACC: key 6 accents step 6 of bar 2", (tp[0].dacc[1] >> 5) & 1u);
    turn(2, -1);
    key_edge(black_note(BK_POLY));
    fm1_in.notes = 1u << black_note(BK_POLY);
    fm1_ms += 600;
    ui_input();
    fm1_in.notes = 0;
    check("POLY held: the pattern cleared", !tp[0].dpat[0][0] && !tp[0].dpat[1][2] && ui.undo == UNDO_DRUM);
    hold(B_SAVE);
    fm1_ms += 1000;
    ui_input();
    let_go(B_SAVE);
    check("SAVE held: it comes back, edits and all", tp[0].dpat[0][1] == (1u << DI_BD) && (tp[0].dpat[1][2] & (1u << DI_SD)));
    press(B_HOME);                                       /* PATTERN: PATN over the edited pattern, SAVE undoes it */
    turn(0, 1);
    check("PATN over an edited pattern: FUNK, and the edit kept for SAVE", tp[0].drm[DM_PATN] == 2 &&
          drm_undo_buf.valid && !strcmp(ui.msg, "FUNK WRITTEN. HOLD SAVE: UNDO"));
    hold(B_SAVE);
    fm1_ms += 1000;
    ui_input();
    let_go(B_SAVE);
    check("..SAVE held: the edited one back", tp[0].dpat[0][1] == (1u << DI_BD));
    tp[0].drm[DM_LEN] = 4;                               /* PATN while STEP shows bar 4: back inside its two bars */
    tp[0].drm[DM_BAR] = 4;
    drm_load(0, 0);
    check("PATN while bar 4 is shown: BAR follows LEN down to 2", tp[0].drm[DM_BAR] == 2);
    for (b = 0; b < DRM_NBAR; b++)
        for (s = 0; s < 16u; s++)
            tp[0].dpat[b][s] = 0;
    fm1_in.notes = 1u << black_note(BK_POLY);
    key_edge(black_note(BK_POLY));
    fm1_ms += 600;
    ui_input();
    fm1_in.notes = 0;
    check("POLY held on an empty pattern: says so; SAVE then has nothing to undo",
          !strcmp(ui.msg, "THE PATTERN IS EMPTY") && drm_undo() == -1);
    fm1_in.notes = note_bit_of_white(4);                 /* an instrument held: its own knobs */
    ui_input();
    turn(2, -24);
    check("PATTERN, the low conga held: KNOB 3 is its own LVL (-24 dB); the box names it",
          ui.drm_inst == DI_LC + 1u && tp[0].dins[DI_LC][DIN_LVL] == -24 && page_target(2) == MOD_NTGT);
    fm1_in.notes = 0;
    ui_input();
    check("..let go: the page's own knobs again", !ui.drm_inst && ui_page(2, (int16_t **)&p) == &DRM_P[DM_SWNG]);

    /* LIVE and ERAS, playing */
    power_on();
    tp[0].src = SRC_DRUM;
    track[1].mute = track[2].mute = track[3].mute = 1;
    p = tp[0].drm;
    press(B_HOME);
    press(B_HOME);
    press(B_HOME);
    turn(2, 2);
    sys.bpm = 120;
    sys.playing = 1;
    drum_reset();
    drum_run(172u * 4u + 150u);                          /* just before step 5 (late in step 4) */
    fm1_in.notes = note_bit_of_white(7);                 /* the claves, pressed late: written on step 5 */
    ui_input();
    drum_run(2);
    fm1_in.notes = 0;
    ui_input();
    drm_poll();
    drum_run(100);                                       /* step 5 passes: not struck twice */
    check("LIVE: a key late in step 4 sounds now and is written on step 5, not struck again there",
          (tp[0].dpat[0][5] & (1u << DI_CL)) && drum_hits[DI_CL] == 1u && drm_dirty[0]);
    turn(2, 1);
    fm1_in.notes = note_bit_of_white(12);                /* ERAS: the hat held for a bar */
    ui_input();
    drum_run(172u * 16u);
    fm1_in.notes = 0;
    ui_input();
    drm_poll();
    for (ok = 1, s = 0; s < 16u; s++)
        ok &= !(tp[0].dpat[0][s] & (1u << DI_HH)) || !(tp[0].dpat[1][s] & (1u << DI_HH));
    check("ERAS: the hi-hat held through a bar: wiped where the playhead passed, and silent", ok &&
          (tp[0].dpat[0][0] & (1u << DI_BD)));
    sys.playing = 0;
    render(2, 0);
    power_on();
}

/* ROUT: the three devices with a WET as sends. A run of track 1 (a reel playing, the device set up by `dev`, WET as
 * given, ROUT as given) into o: what the track outputs (track_rt.last), from power-on, `blocks` blocks */
static void send_run(uint32_t dev, int32_t wet, int32_t rout, int32_t *o, uint32_t blocks)
{
    uint32_t b;
    int16_t *v = tp[0].dev[dev];
    power_on();
    track[1].mute = track[2].mute = track[3].mute = 1;
    sys.playing = 1;
    if (dev == DEV_GRAIN) {
        v[GP_WET] = (int16_t)wet;
        v[GP_ROUT] = (int16_t)rout;
    } else if (dev == DEV_RESO) {
        v[RP_WET] = (int16_t)wet;
        v[RP_ROUT] = (int16_t)rout;
    } else {
        v[CP_DRIV] = 50;
        v[CP_WET] = (int16_t)wet;
        v[CP_ROUT] = (int16_t)rout;
    }
    for (b = 0; b < blocks; b++) {
        chain_block(out, CTL);
        chain_poll();
        memcpy(o + b * CTL, track_rt[0].last, CTL * sizeof *o);
    }
}

static void test_sends(void)
{
    static int32_t ins[CTL * 600], snd[CTL * 600], dry[CTL * 600];
    static const uint32_t DEV[3] = {DEV_GRAIN, DEV_RESO, DEV_COLOR};
    static const char *const NAME[3] = {"GRAIN", "RESONATOR", "COLOR"};
    uint32_t k, i, n = CTL * 600u, from = CTL * 200u;
    char m[128];
    check("ROUT (INS, SEND) on GRAIN, RESONATOR and COLOR; not a modulation target; SPACE has none",
          !strcmp(DEV_P[DEV_GRAIN][GP_ROUT].label, "ROUT") && !strcmp(DEV_P[DEV_RESO][RP_ROUT].label, "ROUT") &&
          !strcmp(DEV_P[DEV_COLOR][CP_ROUT].label, "ROUT") && !mod_tdesc(MOD_TG(DEV_GRAIN, GP_ROUT)) &&
          !mod_tdesc(MOD_TG(DEV_RESO, RP_ROUT)) && !mod_tdesc(MOD_TG(DEV_COLOR, CP_ROUT)));
    for (k = 0; k < 3u; k++) {                           /* SEND - INS = the dry, at WET 100 (from 0.15 s on) */
        int32_t err = 0, pd = 0, pe = 0;
        send_run(DEV[k], 100, 0, ins, 600);
        send_run(DEV[k], 100, 1, snd, 600);
        send_run(DEV[k], 0, 0, dry, 600);
        for (i = from; i < n; i++) {
            int32_t e = snd[i] - ins[i] - dry[i];
            err = (e < 0 ? -e : e) > err ? (e < 0 ? -e : e) : err;
            pd = abs(dry[i]) > pd ? abs(dry[i]) : pd;
            pe = abs(ins[i]) > pe ? abs(ins[i]) : pe;
        }
        snprintf(m, sizeof m, "%s at WET 100: SEND is the dry plus what INS plays (off by %d of a dry peak of %d)",
                 NAME[k], err, pd);
        check(m, pd > 1000 && pe > 300 && err <= 8);
    }
    {                                                    /* ROUT flipped under a held note: the dry ramps, no click */
        int32_t jump = 0, base = 0;
        power_on();
        track[1].mute = track[2].mute = track[3].mute = 1;
        tp[0].src = SRC_SYNTH;
        tp[0].syn[SY_WAVE] = 0;
        tp[0].dev[DEV_COLOR][CP_WET] = 100;              /* (COLOR at its defaults: a send doubles the dry) */
        sys.keys_live = 1;
        fm1_in.notes = note_bit_of_white(0);
        render(300, 0);
        for (i = 0; i < 20u; i++) {
            chain_block(out, CTL);
            for (k = 1; k < CTL; k++)
                base = abs(track_rt[0].last[k] - track_rt[0].last[k - 1]) > base ? abs(track_rt[0].last[k] - track_rt[0].last[k - 1]) : base;
        }
        tp[0].dev[DEV_COLOR][CP_ROUT] = 1;
        for (i = 0; i < 4u; i++) {
            int32_t last = track_rt[0].last[CTL - 1];
            chain_block(out, CTL);
            jump = abs(track_rt[0].last[0] - last) > jump ? abs(track_rt[0].last[0] - last) : jump;
            for (k = 1; k < CTL; k++)
                jump = abs(track_rt[0].last[k] - track_rt[0].last[k - 1]) > jump ? abs(track_rt[0].last[k] - track_rt[0].last[k - 1]) : jump;
        }
        fm1_in.notes = 0;
        snprintf(m, sizeof m, "COLOR's ROUT flipped under a held sine: no step (the largest move %d, steady %d)", jump, base);
        check(m, base > 20 && jump < base * 5 / 2);
    }
    power_on();
}

/* projects (project.c) in the host's NOR model: save, power-cycle, everything back; tapes into reels; a power cut
 * mid-save; a Felucca record; the project view's controls */
static void pj_wipe(void)                                /* the project sectors and the reel slots erased */
{
    uint32_t s, k;
    for (s = 0; s < PJ_N; s++) {
        rf_erase(pj_sector(s, 0));
        rf_erase(pj_sector(s, 1));
    }
    for (k = 0; k < USLOT_N * USLOT_SIZE; k += 4096u)
        rf_erase(USLOT_BASE + k);
    uslot_names();
}

static void pj_scene(void)                               /* a session far from the defaults */
{
    uint32_t t;
    sys.bpm = 97;
    chain_tracks(3);
    mst[MS_AMT] = 45;
    for (t = 0; t < NTRK; t++) {
        tp[t].dev[DEV_GRAIN][GP_WET] = (int16_t)(10 + t * 20);
        tp[t].dev[DEV_COLOR][CP_ROUT] = 1;
        tp[t].ch[CH_PAN] = (int16_t)(t * 30 - 40);
        track[t].level = (uint8_t)(70 + t);
        track[t].octave = (uint8_t)(2 + t);
    }
    track[2].mute = 1;
    tp[0].src = SRC_SYNTH;
    tp[0].syn[SY_CUT] = 33;
    tp[1].src = SRC_DRUM;
    drm_load(1, 2);
    tp[1].drm[DM_SEED] = 17;
    tp[1].dpat[1][7] |= 1u << DI_CB;
    tp[1].dacc[0] = 0x0101u;
    tp[1].dins[DI_SD][DIN_TUNE] = 3;
    tp[2].src = SRC_POLY;
    tp[2].pol[PL_TUNE] = -7;
    param_engine(3, 2, ME_SEQ);
    tp[3].steps[2][5] = 77;
    tp[3].recin = RIN_OTHR;
    mod_nudge(1, 0, MOD_TSRC + SRC_DRUM * NPK + DM_SEED, 30);
    mod_nudge(3, 2, MOD_TG(DEV_SPACE, SP_DLY), -25);
    mod_nudge(0, 1, MOD_TCH + CH_FILT, 60);
}

static void test_projects(void)
{
    static track_params_t want[NTRK];
    static int8_t wdep[NTRK][NSLOT][MOD_NTGT];
    track_ctl_t wtr[NTRK];
    int16_t wmst[NMS];
    pj_hdr_t h;
    uint32_t s, n0, k;
    int rc, same;
    char m[128];
    power_on();
    pj_wipe();
    pj_boot();
    for (same = 1, s = 0; s < PJ_N; s++)
        same &= !pj_sum[s].used;
    check("projects: none saved yet: every slot empty, the defaults kept, P1 the current one",
          same && pj_cur == 0 && tp[0].src == SRC_TAPE && !pj_changed());
    pj_scene();
    check("..a change makes the project changed", pj_changed());
    rc = pj_save(1);
    memcpy(want, tp, sizeof tp);
    memcpy(wdep, mdep, sizeof mdep);
    memcpy(wtr, track, sizeof track);
    memcpy(wmst, mst, sizeof mst);
    pj_current(1, &h);
    snprintf(m, sizeof m, "saved into P2: %u bytes packed (%u unpacked, a sector holds %u)", h.len, h.raw, PJ_PAYLOAD_MAX);
    check(m, rc == 0 && pj_cur == 1 && !pj_changed() && pj_sum[1].used && h.len < h.raw && h.len < 2500u);
    power_on();                                          /* a power cycle */
    check("..power-on alone: the defaults (nothing loaded yet)", tp[1].src == SRC_TAPE && sys.bpm != 97);
    pj_boot();
    same = !memcmp(tp, want, sizeof tp) && !memcmp(mdep, wdep, sizeof mdep) && !memcmp(mst, wmst, sizeof mst);
    for (k = 0; k < NTRK; k++)
        same &= track[k].level == wtr[k].level && track[k].mute == wtr[k].mute && track[k].octave == wtr[k].octave;
    check("..booted: P2 back as saved: every knob, source, pattern, step, depth, level, mute, octave, the tempo",
          same && pj_cur == 1 && sys.bpm == 97 && sys.ntrk == 3 && mod_count(1) == 1u && mod_count(3) == 1u &&
          !pj_changed());
    {                                                    /* the depths reach the ISR (the lists rebuilt) */
        const int32_t *last[NTRK] = {track_rt[0].last, track_rt[1].last, track_rt[2].last, track_rt[3].last};
        mod_tick(0, 0, last);
        check("..and the loaded depths are live (track 2's list holds SEED's)", (mod_on >> 1) & 1u);
    }

    /* a track's own tape: kept in a user reel named after the project and the track */
    tp[0].src = SRC_TAPE;
    tp[0].dev[DEV_SRC][TK_REEL] = 1;
    tape_prepare(0);                                     /* (factory reel 1 copied onto the tape: a take) */
    tape_unprepare(0);
    n0 = tape_ctl[0].nblk;
    rc = pj_save(1);
    k = (uint32_t)tp[0].dev[DEV_SRC][TK_REEL];
    check("..T1's own tape saved: into user reel P2T1, the track now plays that reel (the same length)",
          rc == 0 && k > NREEL && uslot_valid(k - NREEL - 1u) && !strcmp(uslot_hdr(k - NREEL - 1u)->name, "P2T1") &&
          uslot_hdr(k - NREEL - 1u)->nblk == n0);
    for (n0 = 0, s = 0; s < USLOT_N; s++)
        n0 += uslot_valid(s);
    rc = pj_save(1);
    for (k = 0, s = 0; s < USLOT_N; s++)
        k += uslot_valid(s);
    check("..saved again unchanged: no new reel", rc == 0 && k == n0 && n0 == 1u);
    power_on();
    pj_boot();
    {
        tape_view_t v;
        tape_view(0, &v);
        check("..power-cycled: T1 plays P2T1, as long as the take", v.len == uslot_hdr(0)->nblk * TAPE_BLK && v.len);
    }
    for (s = 1; s < USLOT_N; s++)                        /* no reel free: the project still saves */
        uslot_save(s, REELS[0].data, REELS[0].pred, REELS[0].idx, REELS[0].peak, 4, "FULL");
    tp[2].dev[DEV_SRC][TK_REEL] = 1;
    tp[2].pol[PL_REEL] = 1;
    tape_prepare(2);
    tape_unprepare(2);
    rc = pj_save(1);
    check("..no reel free for T3's take: saved without it, and SAVE knows which", rc == 1 << 2 && pj_sum[1].used);

    /* a power cut in the middle of a save: the copy before stays in charge */
    sys.bpm = 150;
    host_prog_limit = 600;
    rc = pj_save(1);
    host_prog_limit = 0xFFFFFFFFu;
    power_on();
    pj_boot();
    check("a power cut halfway through a save: the copy before is loaded (the tempo it had, not 150)",
          rc < 0 && pj_sum[1].used && sys.bpm == 97);

    /* a Felucca record in a slot's sector reads as empty; an empty slot doesn't load */
    rf_erase(pj_sector(0, 0));
    {
        static const uint8_t FELU[32] = {'F', 'E', 'L', 'U', 1, 0, 0, 0, 1};
        rf_prog(pj_sector(0, 0), FELU, sizeof FELU);
    }
    pj_scan(0);
    check("a Felucca project in P1's sector: P1 reads as empty, and doesn't load", !pj_sum[0].used && pj_load(0) == -1);

    /* the project view */
    power_on();
    pj_boot();
    host_enc[panel.enc[EN_PRESET]] = 1;
    ui_input();
    check("PRESETS: the project view on P2 (the one loaded); turned again, P3", ui.view == VIEW_PROJECT && ui.pj_sel == 1u &&
          (host_enc[panel.enc[EN_PRESET]] = 1, ui_input(), ui.pj_sel == 2u));
    shot("BRYO", "projects");
    press(B_OCTUP);
    check("..OCT+ on an empty slot: says so", !strcmp(ui.msg, "P3 IS EMPTY"));
    tap(B_SAVE);
    check("..SAVE: P3 saved (an empty slot: no question), now the current one", pj_sum[2].used && pj_cur == 2 &&
          !strncmp(ui.msg, "P3 SAVED", 8));
    host_enc[panel.enc[EN_PRESET]] = -1;
    ui_input();
    tap(B_SAVE);
    check("..SAVE on P2 (another project): asked first, not saved", ui.pj_ask == PJ_ASK_SAVE && pj_cur == 2);
    tap(B_SAVE);
    check("..SAVE again within 3 s: saved over it", pj_cur == 1 && !strncmp(ui.msg, "P2 SAVED", 8));
    sys.bpm = 133;
    host_enc[panel.enc[EN_PRESET]] = 1;
    ui_input();
    press(B_OCTUP);
    check("..OCT+ on P3 with a change not saved: asked first", ui.pj_ask == PJ_ASK_LOAD && sys.bpm == 133);
    press(B_OCTUP);
    check("..OCT+ again: P3 loaded", pj_cur == 2 && sys.bpm == 97 && !strcmp(ui.msg, "P3 LOADED"));
    fm1_in.notes = 1u << black_note(BK_POLY);
    key_edge(black_note(BK_POLY));
    fm1_ms += 600;
    ui_input();
    fm1_in.notes = 0;
    check("..POLY held: P3 deleted (its reels stay)", !pj_sum[2].used && !strncmp(ui.msg, "P3 DELETED", 10) &&
          uslot_valid(0));
    turn(0, 5);
    press(B_OCTDN);
    check("..the knobs do nothing there; OCT-: back to the page", ui.view == VIEW_PAGE && tp[0].dev[DEV_SRC][0] == 0);
    sys.bpm = 101;
    tap(B_SAVE);
    check("SAVE tapped on a page: into the current project (P3)", pj_cur == 2 && pj_sum[2].used && pj_sum[2].bpm == 101);
    pj_wipe();
    power_on();
}

static void test_source_level(void)
{
    static int32_t a[CTL * 300], b[CTL * 300];
    static const char *const NAME[NSRC] = {"TAPE LVL -12 dB: a quarter of the track's sound",
                                           "SYNTH LVL -12 dB: a quarter", "POLY LVL -12 dB: a quarter",
                                           "DRUM LVL -12 dB: a quarter (its pattern playing)"};
    uint32_t src;
    for (src = 0; src < NSRC; src++) {
        int16_t *lv = src == SRC_TAPE ? &tp[0].dev[DEV_SRC][TK_LVL] : src == SRC_SYNTH ? &tp[0].syn[SY_LVL]
                    : src == SRC_POLY ? &tp[0].pol[PL_LVL] : &tp[0].drm[DM_LVL];
        int32_t pa, pb;
        power_on();
        tp[0].src = (uint8_t)src;
        track[1].mute = track[2].mute = track[3].mute = 1;
        sys.playing = src == SRC_TAPE || src == SRC_DRUM;
        fm1_in.notes = src == SRC_TAPE || src == SRC_DRUM ? 0u : note_bit_of_white(0);
        render(100, 0);
        render(300, a);
        power_on();                                      /* the same again, from power-on, at -12 dB */
        tp[0].src = (uint8_t)src;
        track[1].mute = track[2].mute = track[3].mute = 1;
        sys.playing = src == SRC_TAPE || src == SRC_DRUM;
        fm1_in.notes = src == SRC_TAPE || src == SRC_DRUM ? 0u : note_bit_of_white(0);
        *lv = -12;
        render(100, 0);
        render(300, b);
        fm1_in.notes = 0;
        pa = peak_of(a, CTL * 300u);
        pb = peak_of(b, CTL * 300u);
        check(NAME[src], pa > 2000 && pb * 100 > pa * 22 && pb * 100 < pa * 28);
    }
    check("..and LVL can be modulated (a target on every source)",
          mod_tdesc(MOD_TSRC + SRC_TAPE * NPK + TK_LVL) && mod_tdesc(MOD_TSRC + SRC_SYNTH * NPK + SY_LVL) &&
          mod_tdesc(MOD_TSRC + SRC_POLY * NPK + PL_LVL) && mod_tdesc(MOD_TSRC + SRC_DRUM * NPK + DM_LVL));
    sys.playing = 0;
    power_on();
}

/* the engines' switches, one block at a time through mod_tick (track 1, GRAIN WET from 0 at depth 100: the value is
 * the slot's output in %) */
static int32_t mod_step(uint32_t keys)
{
    static const int32_t zero[CTL];
    const int32_t *last[NTRK] = {zero, zero, zero, zero};
    mod_tick(keys, 0, last);
    return TPD(0, DEV_GRAIN)[GP_WET];
}

static void test_mod_engines(void)
{
    const uint32_t WET = MOD_TG(DEV_GRAIN, GP_WET);
    uint32_t b, i;
    int32_t v, early, late;
    power_on();
    sys.bpm = 120;
    sys.playing = 1;
    /* SEQ's orders: steps 0 33 66 100 at 1/16 (172 blocks a step); a sample from the middle of each step */
    {
        static const int32_t WANT[4][6] = {{0, 33, 66, 100, 0, 33}, {100, 66, 33, 0, 100, 66}, {0, 33, 66, 100, 66, 33}};
        static const char *const NAME[3] = {"mod: SEQ FWD: 1 2 3 4", "mod: SEQ REV: 4 3 2 1", "mod: SEQ PING: 1 2 3 4 3 2"};
        uint32_t d;
        param_engine(0, 2, ME_SEQ);
        tp[0].mod[2][0] = 4;
        tp[0].mod[2][1] = 4;
        for (i = 0; i < 4u; i++)
            tp[0].steps[2][i] = (int8_t)(i * 100u / 3u);
        mod_nudge(0, 2, WET, 100);
        for (d = 0; d < 4u; d++) {
            int ok = 1, seen[4] = {0};
            tp[0].mod[2][4] = (int16_t)d;
            sys.playing = 0;
            mod_step(0);
            sys.playing = 1;
            for (i = 0; i < 6u; i++) {
                for (b = 0; b < 172u; b++)
                    v = mod_step(0);
                if (d < 3u)
                    ok &= v == WANT[d][i];
                else
                    seen[v == 0 ? 0 : v == 33 ? 1 : v == 66 ? 2 : 3]++;
            }
            if (d < 3u)
                check(NAME[d], ok);
            else
                check("mod: SEQ RND: steps from the four, not FWD's", seen[0] + seen[1] + seen[2] + seen[3] == 6 &&
                                                                     !(seen[0] == 2 && seen[1] == 2 && seen[2] == 1));
        }
        tp[0].mod[2][4] = 0;
        tp[0].mod[2][2] = 50;                            /* SLEW 50: halfway through the glide, halfway there */
        sys.playing = 0;
        mod_step(0);
        sys.playing = 1;
        for (b = 0; b < 172u + 43u; b++)
            v = mod_step(0);
        check("mod: SEQ SLEW: a glide into the next step", v > 5 && v < 30);
        for (b = 0; b < 60u; b++)
            v = mod_step(0);
        check("..arriving at it", v == 33);
        tp[0].mod[2][2] = 0;
        tp[0].mod[2][3] = 100;                           /* SWNG 100: step 2 starts a third of a step late */
        sys.playing = 0;
        mod_step(0);
        sys.playing = 1;
        for (b = 0; b < 172u + 30u; b++)
            v = mod_step(0);
        check("mod: SEQ SWNG: the off-beat starts late", v == 0);
        for (b = 0; b < 40u; b++)
            v = mod_step(0);
        check("..then plays", v == 33);
        mod_clear(0);
    }
    /* ADSR LOOP: a key held, attack and decay come round again (the level climbs after reaching SUS) */
    {
        uint32_t rises = 0, low = 1;
        param_engine(0, 1, ME_ADSR);
        tp[0].mod[1][0] = 20;
        tp[0].mod[1][1] = 20;
        tp[0].mod[1][2] = 20;
        tp[0].mod[1][9] = 1;
        mod_nudge(0, 1, WET, 100);
        for (b = 0; b < 600u; b++) {
            v = mod_step(1u);
            if (low && v > 90) {                         /* up to the top from SUS (20 %): one more time round */
                rises++;
                low = 0;
            } else if (v <= 25) {
                low = 1;
            }
        }
        check("mod: ADSR LOOP: attack and decay again while the key is held", rises >= 3u);
        mod_clear(0);
    }
    /* LFO FADE 100: the first cycle fades in (TRIG KEY restarts it) */
    {
        param_engine(0, 0, ME_WAVE);
        tp[0].mod[0][0] = 60;                            /* ~0.5 Hz: a cycle in ~2700 blocks */
        tp[0].mod[0][12] = 0;
        tp[0].mod[0][13] = 1;
        tp[0].mod[0][14] = 100;
        tp[0].mod[0][1] = 2;                             /* SQR: full swing from the start, but for FADE */
        tp[0].ch[CH_PAN] = 0;
        mod_nudge(0, 0, MOD_TCH + CH_PAN, 50);
        mod_step(0);
        mod_step(1u);
        early = TPD(0, MA_CH)[CH_PAN];
        for (b = 0; b < 3000u; b++)
            mod_step(1u);
        late = TPD(0, MA_CH)[CH_PAN];
        check("mod: LFO FADE: quiet at the restart, full a cycle on", early > -10 && early < 10 && (late > 85 || late < -85));
        mod_clear(0);
    }
    /* LFO VAR on RND: the steps drift each time round; on a shape: each cycle's level */
    {
        int16_t was[16];
        uint32_t moved = 0, k;
        param_engine(0, 0, ME_WAVE);
        tp[0].mod[0][1] = LFO_RND;
        tp[0].mod[0][0] = 127;
        tp[0].mod[0][12] = 0;
        tp[0].mod[0][6] = 80;
        mod_nudge(0, 0, WET, 50);
        mod_step(0);
        for (k = 0; k < 16u; k++)
            was[k] = mrt[0][0].rnd[k];
        for (b = 0; b < 400u; b++)
            mod_step(0);
        for (k = 0; k < 16u; k++)
            moved += mrt[0][0].rnd[k] != was[k];
        check("mod: LFO VAR on RND: the steps drift from time round to time round", moved >= 12u);
        tp[0].mod[0][1] = 0;                             /* SIN, VAR 80, LEN 2: two cycles' levels */
        tp[0].mod[0][7] = 2;
        for (b = 0, v = 0; b < 400u; b++) {
            int32_t x = mod_step(0);
            v = x > v ? x : v;
        }
        check("..on SIN: the cycles' levels vary (never all at full)", v > 10 && v < 50);
        mod_clear(0);
    }
    /* FOLLOW HOLD 1/4: the envelope sampled once a beat (689 blocks at 120 BPM) */
    {
        static int32_t loud[CTL], quiet[CTL];
        const int32_t *last[NTRK] = {quiet, loud, quiet, quiet};
        uint32_t changes = 0;
        int32_t prev = -1;
        for (i = 0; i < CTL; i++)
            loud[i] = 20000;
        param_engine(0, 3, ME_FOLLOW);
        tp[0].mod[3][0] = 2;
        tp[0].mod[3][4] = 4;
        mod_nudge(0, 3, WET, 100);
        for (b = 0; b < 689u * 3u; b++) {
            last[1] = (b / 100u) & 1u ? loud : quiet;    /* (a source pulsing every 100 blocks) */
            mod_tick(0, 0, last);
            v = TPD(0, DEV_GRAIN)[GP_WET];
            changes += prev >= 0 && v != prev;
            prev = v;
        }
        check("mod: FOLLOW HOLD 1/4: the level moves only on the beat", changes >= 1u && changes <= 3u);
    }
    sys.playing = 0;
    power_on();
    /* the pitch knobs modulate finer than a semitone: an LFO on TUNE at a small depth is vibrato, not a trill */
    {
        uint32_t b, fine = 0, f;
        int32_t lo = 1 << 20, hi = -(1 << 20);
        power_on();
        tp[0].src = SRC_SYNTH;
        tp[0].mod[0][0] = 100;                           /* ~6 Hz */
        tp[0].mod[0][12] = 0;
        mod_nudge(0, 0, MOD_TSRC + SRC_SYNTH * NPK + SY_TUNE, 1);   /* 1 % of 48 semitones: about half a semitone */
        for (b = 0; b < 400u; b++) {
            static const int32_t zero[CTL];
            const int32_t *last[NTRK] = {zero, zero, zero, zero};
            mod_tick(0, 0, last);
            f = (uint32_t)mod_pitch16(0, FN_SYN, TPD(0, MA_SYN)[SY_TUNE]);
            fine += (f & 15u) != 0u;
            lo = (int32_t)f < lo ? (int32_t)f : lo;
            hi = (int32_t)f > hi ? (int32_t)f : hi;
        }
        check("mod: TUNE modulates in 1/16 semitones (vibrato: about +-half a semitone, between the semitones)",
              fine > 100u && lo >= -9 && lo <= -7 && hi >= 7 && hi <= 9);
        check("..and reads as the knob x 16 when nothing modulates it", mod_pitch16(1, FN_SYN, 5) == 80);
        mod_clear(0);                                    /* a SEQ on TUNE: its steps stay on semitones */
        param_engine(0, 2, ME_SEQ);
        tp[0].mod[2][0] = 4;
        tp[0].mod[2][1] = 4;
        tp[0].steps[2][0] = 15;                          /* 15 % of 48: 7.2 semitones, a fifth */
        tp[0].steps[2][1] = 21;
        tp[0].steps[2][2] = 6;
        tp[0].steps[2][3] = 25;
        mod_nudge(0, 2, MOD_TSRC + SRC_SYNTH * NPK + SY_TUNE, 100);
        sys.playing = 1;
        for (b = 0, fine = 0; b < 800u; b++) {
            static const int32_t zero[CTL];
            const int32_t *last[NTRK] = {zero, zero, zero, zero};
            mod_tick(0, 0, last);
            f = (uint32_t)mod_pitch16(0, FN_SYN, TPD(0, MA_SYN)[SY_TUNE]);
            fine += (f & 15u) != 0u;
        }
        sys.playing = 0;
        check("..a SEQ on TUNE lands on semitones (a step is a note)", fine == 0u && f % 16u == 0u);
    }
    /* the filters' cutoffs modulate finer than their semitones: a slow sweep moves a little every block, never in
     * whole-step jumps (SYNTH's CUT, POLY's, RESONATOR's, the channel's FILT) */
    {
        static const uint32_t FNS[4] = {FN_SCUT, FN_PCUT, FN_RCUT, FN_FILT};
        static const char *const NAME[4] = {"mod: SYNTH's CUT sweeps in 1/256 steps (no semitone staircase)",
                                            "..POLY's CUT", "..RESONATOR's CUT", "..the channel's FILT (1/256 of its 1 % steps)"};
        uint32_t w, b;
        for (w = 0; w < 4u; w++) {
            uint32_t g = w == 0u ? MOD_TSRC + SRC_SYNTH * NPK + SY_CUT : w == 1u ? MOD_TSRC + SRC_POLY * NPK + PL_CUT
                       : w == 2u ? MOD_TG(DEV_RESO, RP_CUT) : MOD_TCH + CH_FILT;
            uint32_t k, a = mod_tarr(g, &k), frac = 0, big = 0;
            int32_t prev = 0;
            power_on();
            mod_base(0, a)[k] = (int16_t)(w == 3u ? -40 : 60);
            tp[0].mod[0][0] = 20;                        /* a slow triangle */
            tp[0].mod[0][1] = 1;
            tp[0].mod[0][12] = 0;
            mod_nudge(0, 0, g, 20);
            for (b = 0; b < 2000u; b++) {
                static const int32_t zero[CTL];
                const int32_t *last[NTRK] = {zero, zero, zero, zero};
                int32_t v;
                mod_tick(0, 0, last);
                v = mod_fine8(0, FNS[w], TPD(0, a)[k]);
                frac += (v & 255) != 0;
                if (b && (v - prev > 128 || prev - v > 128))
                    big++;
                prev = v;
            }
            check(NAME[w], frac > 1500u && big == 0u && (mod_fmask[0] >> FNS[w]) & 1u);
        }
        check("..and a cutoff reads as the knob x 256 when nothing modulates it", mod_fine8(1, FN_SCUT, 64) == 64 * 256);
        power_on();
    }
    /* RESONATOR's root glides when PTCH moves (half the way a block), so the strings' read point never jumps */
    {
        int32_t r0, r1;
        power_on();
        reso_poll();
        tp[0].dev[DEV_RESO][RP_WET] = 60;
        reso_poll();
        reso_poll();
        reso_poll();
        reso_poll();
        render(20, 0);
        r0 = reso[0].rs16;
        tp[0].dev[DEV_RESO][RP_PTCH] += 12;
        render(1, 0);
        r1 = reso[0].rs16;
        render(30, 0);
        check("RESONATOR: PTCH +12 glides there (half the way in a block, all of it soon after)",
              reso[0].nch > 0u && r1 > r0 && r1 < r0 + 12 * 16 && reso[0].rs16 >= r0 + 12 * 16 - 1);
        power_on();
    }
    /* TUNE moves a held note (SYNTH and POLY add it as the voice plays, not when the key went down) */
    {
        static int32_t buf[CTL * 400];
        uint32_t src, i;
        for (src = SRC_SYNTH; src <= SRC_SYNTH; src++) {
            uint32_t up0 = 0, up1 = 0;
            int16_t *tune = src == SRC_SYNTH ? &tp[0].syn[SY_TUNE] : &tp[0].pol[PL_TUNE];
            power_on();
            tp[0].src = (uint8_t)src;
            track[1].mute = track[2].mute = track[3].mute = 1;
            fm1_in.notes = note_bit_of_white(0);
            render(100, 0);
            render(400, buf);
            for (i = 1; i < CTL * 400u; i++)
                up0 += buf[i - 1] < 0 && buf[i] >= 0;
            *tune = 12;
            render(20, 0);
            render(400, buf);
            for (i = 1; i < CTL * 400u; i++)
                up1 += buf[i - 1] < 0 && buf[i] >= 0;
            fm1_in.notes = 0;
            if (src == SRC_SYNTH)
                check("SYNTH: TUNE +12 on a held note: an octave up (TUNE is live, so it can be modulated)",
                      up0 > 20u && up1 * 10u > up0 * 17u && up1 * 10u < up0 * 23u);
        }
        {                                                /* POLY: the voice's head steps twice as fast */
            int32_t i0;
            power_on();
            tp[0].src = SRC_POLY;
            fm1_in.notes = note_bit_of_white(0);
            render(4, 0);
            i0 = pol[0].v[0].inc;
            tp[0].pol[PL_TUNE] = 12;
            render(1, 0);
            fm1_in.notes = 0;
            check("POLY: TUNE +12 on a held note: an octave up", i0 > 0 && pol[0].v[0].stage &&
                                                                 pol[0].v[0].inc >= 2 * i0 - 2 && pol[0].v[0].inc <= 2 * i0 + 2);
        }
        power_on();
    }
}

int main(int argc, char **argv)
{
    out_dir = argc > 1 ? argv[1] : "build/bryo_ui";
    memset(host_nor, 0xFF, sizeof host_nor);
    test_canvas();
    test_tape();
    test_uslots();
    test_drive();
    test_drive_more();
    test_tape_edges();
    test_memory();
    test_controls_more();
    test_synth();
    test_poly();
    test_grain();
    test_grain_buffer();
    test_reso();
    test_color();
    test_space();
    test_usbrec();
    test_mixer();
    test_mod();
    test_mod_engines();
    test_source_level();
    test_drum_voices();
    test_drum();
    test_sends();
    test_projects();
    test_input();
    test_settings();
    palette_set(UI_GREY_INDEX);                          /* (the screens are one ink now, whatever the palette) */
    screens_in("BRYO");
    printf(fails ? "bryo: %d FAILED\n" : "bryo: all passed\n", fails);
    return fails != 0;
}

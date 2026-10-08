/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: buttons, keys and encoders into actions, and the LEDs. Main loop only.
 *
 * Phase 1 mapping (docs/bryo-architecture.md, PRD 2):
 *   HOME           focus the track's source (TAPE); again: its page 2
 *   EDIT           focus GRAIN; again: GRAIN 2, RESONATOR, GRAIN ... (each device's pages, then the next device)
 *   FX             focus COLOR; again: COLOR 2, SPACE, SPACE 2, COLOR ...
 *   LFO ENV SEQ ARP  focus modulator slot 1..4; again: the slot's next page; held + SELECT: the slot's engine
 *   GLO held       the mixer while held: white keys 1..4 pick the track, KNOB 1..4 set the levels; let go: back
 *   GLO tapped     the mixer stays up; tap again (or any page pad): back
 *   EDIT held      on the mixer: KNOB 1..4 set the selected track's channel (LOW HIGH FILT PAN); let go: levels
 *   SCL            unassigned (the PRD's SEL; track picking moved under GLO)
 *   PLAY           start / stop
 *   REC            arm the focused track (recording arrives with TAPE, phase 2)
 *   OCT- / OCT+    the white keys' octave
 *   KNOB 1..4      the four values on screen; SELECT: the tempo
 *   black OP1..OP4 track mutes
 * What the later phases add (holding a slot pad to set depths, the views behind PRESETS and ALGORITHM, the
 * other black keys) slots into the same functions. */

/* ------------------------------------------------------------ LEDs --- */
static uint8_t led_pos[41];          /* button 0..13 / key 14..40 -> (column << 3) | row; 0xFF none */
static void led_pos_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < 41u; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
}
static void led_put(uint8_t *nl, uint32_t id, int on)
{
    uint8_t q = led_pos[id];
    if (q != 0xFF && on)
        nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
}
/* PLAY's second, green LED: not in the key matrix, found on the hardware at column 8, row PA9 (bit 1) */
#define LED_PLAY_GREEN ((8u << 3) | 1u)
static void led_clear(uint8_t *nl, uint32_t id)
{
    uint8_t q = led_pos[id];
    if (q != 0xFF)
        nl[q >> 3] &= (uint8_t)~(1u << (q & 7u));
}

/* the pad that focuses what the screen shows */
static uint32_t focus_btn(void)
{
    static const uint8_t SLOT_BTN[NSLOT] = {B_LFO, B_ENV, B_SEQ, B_ARP};
    if (ui.view == VIEW_MIXER)
        return ui.chan ? B_EDIT : B_GLO;
    if (ui.kind == FOCUS_SLOT)
        return SLOT_BTN[ui.slot];
    return ui.dev == DEV_SRC ? B_HOME : ui.dev <= DEV_RESO ? B_EDIT : B_FX;
}

/* Lit: the focus pad, REC armed, the octave keys away from the default, the muted tracks' OP keys. The rest glow
 * dim (the LEDS setting: OFF, DIM LO, DIM HI, INV). Each picture is built off-line and copied one
 * byte per column, the glow first, so an LED going from lit to dim never has a dark frame. */
static void ui_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, nd[FM1_NCOL] = {0};
    uint32_t k, c, mode = settings_leds;
    static uint8_t ready;
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    led_put(nl, panel.btn[focus_btn()], 1);
    led_put(nl, panel.btn[B_REC], (sys.rec >> sys.sel) & 1u);
    led_put(nl, panel.btn[B_OCTDN], track[sys.sel].octave < 3u);
    led_put(nl, panel.btn[B_OCTUP], track[sys.sel].octave > 3u);
    for (k = 0; k < 27u; k++) {
        uint32_t b = KEY_BLACK[k];
        led_put(nl, 14u + k, b <= BK_OP4 && track[b].mute);
        led_put(nd, 14u + k, 1);
    }
    for (k = 0; k < NB; k++)
        led_put(nd, panel.btn[k], 1);
    if (sys.playing)                                    /* playing: PLAY's green, its own LED dark in every mode */
        led_clear(nd, panel.btn[B_PLAY]);
    for (c = 0; c < FM1_NCOL; c++) {
        if (mode == LEDS_INV)                           /* INV: the active ones dark, the rest lit */
            nl[c] = (uint8_t)(nd[c] & ~nl[c]);
        nd[c] = mode == LEDS_DIM || mode == LEDS_DIM_LO ? nd[c] : 0u;   /* OFF, INV: no glow */
    }
    if (sys.playing)
        nl[LED_PLAY_GREEN >> 3] |= (uint8_t)(1u << (LED_PLAY_GREEN & 7u));
    fm1_led_dim_level(mode == LEDS_DIM_LO);
    for (c = 0; c < FM1_NCOL; c++)
        fm1_led_dim[c] = nd[c];
    for (c = 0; c < FM1_NCOL; c++)
        fm1_led[c] = nl[c];
}

/* ----------------------------------------------------------- actions --- */
/* a page pad: that page (the mixer closes); the visualization starts without a last-turned knob */
static void focus_dev(uint32_t d, uint32_t page)
{
    ui.view = VIEW_PAGE;
    ui.chan = 0;
    ui.glo_latched = 0;
    ui.kind = FOCUS_DEV;
    ui.dev = (uint8_t)d;
    ui.page = (uint8_t)page;
    ui.last = 0xFF;
}

/* a pad that holds devices a and b (b == a: one device): pressed again, the next page, then the next device,
 * round; from elsewhere, a's first page */
static void pad_devices(uint32_t a, uint32_t b)
{
    uint32_t d = ui.dev;
    if (ui.view != VIEW_PAGE || ui.kind != FOCUS_DEV || (d != a && d != b)) {
        focus_dev(a, 0);
        return;
    }
    if (ui.page + 1u < pdesc_pages(dev_p(sys.sel, d)))
        focus_dev(d, ui.page + 1u);
    else
        focus_dev(d == a ? b : a, 0);
}

static void focus_slot(uint32_t s)
{
    uint32_t again = ui.view == VIEW_PAGE && ui.kind == FOCUS_SLOT && ui.slot == s;
    ui.view = VIEW_PAGE;
    ui.chan = 0;
    ui.glo_latched = 0;
    ui.kind = FOCUS_SLOT;
    ui.slot = (uint8_t)s;
    ui.page = again ? (uint8_t)((ui.page + 1u) % pdesc_pages(ME_P[tp[sys.sel].engine[s]])) : 0u;
    ui.last = 0xFF;
}

/* GLO down: the mixer comes up and stays while GLO is held */
static void glo_down(void)
{
    ui.glo_held = 1;
    ui.glo_used = 0;
    ui.glo_t0 = fm1_ms;
    ui.view = VIEW_MIXER;
    ui.last = 0xFF;
}

/* GLO up: after a hold that did something, or a long hold, back to the page; after a tap, the mixer stays (a tap
 * on a mixer that was already up closes it) */
static void glo_up(void)
{
    int tap = !ui.glo_used && (uint32_t)(fm1_ms - ui.glo_t0) < HOLD_MS[settings_hold % 4u];
    ui.glo_held = 0;
    if (tap && !ui.glo_latched) {
        ui.glo_latched = 1;
        return;
    }
    ui.glo_latched = 0;
    ui.view = VIEW_PAGE;
    ui.chan = 0;
    ui.last = 0xFF;
}

static void on_button(uint32_t b)
{
    switch (b) {
    case B_HOME:                                        /* TAPE, its page 2 */
        pad_devices(DEV_SRC, DEV_SRC);
        break;
    case B_EDIT:                                        /* GRAIN, GRAIN 2, RESONATOR; on the mixer: the channel, held */
        if (ui.view == VIEW_MIXER) {
            ui.chan = 1;
            ui.last = 0xFF;
            if (ui.glo_held)
                ui.glo_used = 1;
            break;
        }
        pad_devices(DEV_GRAIN, DEV_RESO);
        break;
    case B_FX:                                          /* COLOR, COLOR 2, SPACE, SPACE 2 */
        pad_devices(DEV_COLOR, DEV_SPACE);
        break;
    case B_LFO: focus_slot(0); break;
    case B_ENV: focus_slot(1); break;
    case B_SEQ: focus_slot(2); break;
    case B_ARP: focus_slot(3); break;
    case B_GLO:
        glo_down();
        break;
    case B_PLAY:
        sys.playing = (uint8_t)!sys.playing;
        break;
    case B_REC:                                         /* arm: the tape made ready (a reel copied in) */
        if ((sys.rec >> sys.sel) & 1u) {
            sys.rec &= (uint8_t)~(1u << sys.sel);
            tape_unprepare(sys.sel);
        } else {
            int r = tape_prepare(sys.sel);
            if (r > 0)
                sys.rec |= (uint8_t)(1u << sys.sel);
            else
                ui_message(r < 0 ? "NO MEMORY FREE TO RECORD" : "TAPE HAS A TAKE: CLEAR IT (HOLD POLY)");
        }
        break;
    case B_SAVE:                                        /* tap: save (phase 8); held: undo the last clear */
        ui.save_held = 1;
        ui.save_t0 = fm1_ms;
        break;
    case B_OCTDN:
        if (track[sys.sel].octave > 1u)
            track[sys.sel].octave--;
        break;
    case B_OCTUP:
        if (track[sys.sel].octave < 6u)
            track[sys.sel].octave++;
        break;
    default:
        break;
    }
}

static void on_black(uint32_t k)
{
    int16_t *tk = tp[sys.sel].dev[DEV_SRC];
    if (k <= BK_OP4) {
        track[k].mute = (uint8_t)!track[k].mute;
    } else if (k == BK_OP5 || k == BK_OP6) {           /* the focused track's tape: reverse, half speed */
        tk[k == BK_OP5 ? TK_REV : TK_HALF] = (int16_t)!tk[k == BK_OP5 ? TK_REV : TK_HALF];
    } else if (k == BK_POLY) {                          /* held HOLD: clear the focused track's tape */
        ui.poly_held = 1;
        ui.poly_t0 = fm1_ms;
        ui_message("KEEP HOLDING POLY TO CLEAR THE TAPE");
    }
}

/* the POLY key and SAVE held long enough: the clear and its undo; let go sooner: the clear is off, SAVE is a tap */
static void hold_keys(void)
{
    uint32_t hold = HOLD_MS[settings_hold % 4u], k;     /* (POLY: half a second, PRD 2.3) */
    if (ui.poly_held) {
        for (k = 0; k < 27u && KEY_BLACK[k] != BK_POLY; k++)
            ;
        if (!((fm1_in.notes >> k) & 1u)) {
            ui.poly_held = 0;
            ui.msg_t = 1;                               /* (the message ends next frame) */
        } else if ((uint32_t)(fm1_ms - ui.poly_t0) >= 500u) {
            char m[40] = "TRACK ";
            ui.poly_held = 0;
            tape_clear(sys.sel);
            sys.rec &= (uint8_t)~(1u << sys.sel);
            fmt_int(m + 6, (int32_t)sys.sel + 1);
            str_cpy(m + str_len(m), " CLEARED. HOLD SAVE: UNDO", 28);
            ui_message(m);
        }
    }
    if (ui.save_held) {
        int held = (int)((fm1_in.buttons >> panel.btn[B_SAVE]) & 1u);
        if (!held) {
            ui.save_held = 0;
            ui_message("SAVE: PROJECTS ARRIVE IN PHASE 8");
        } else if ((uint32_t)(fm1_ms - ui.save_t0) >= hold) {
            ui.save_held = 0;
            ui_message(tape_undo_clear() >= 0 ? "CLEAR UNDONE" : "NOTHING TO UNDO");
        }
    }
}

/* KNOB c turned by d detents: the value on screen; it lights up (hot) for half a second */
static void on_knob(uint32_t c, int32_t d)
{
    int16_t *vp;
    const pdesc_t *p = ui_page(c, &vp);
    int16_t v;
    if (pdesc_empty(p))                                 /* an unused knob on a page */
        return;
    v = param_nudge(p, *vp, d);
    if (ui.view == VIEW_MIXER && !ui.chan)
        track[c].level = (uint8_t)v;
    else
        *vp = v;
    ui.hot = (uint8_t)c;
    ui.hot_t = 33;
    ui.last = (uint8_t)c;
    if (ui.glo_held)
        ui.glo_used = 1;
}

static void ui_input(void)
{
    uint32_t released = 0, pressed = fm1_input_edges(&released), notes = fm1_input_note_edges(), id, k;
    int32_t d;
    for (id = 0; id < NB; id++)
        if ((pressed >> id) & 1u) {
            uint32_t b = panel_btn_of(id);
            if (b < NB)
                on_button(b);
        }
    if (ui.chan && (((released >> panel.btn[B_EDIT]) & 1u) || !((fm1_in.buttons >> panel.btn[B_EDIT]) & 1u))) {
        ui.chan = 0;                                    /* EDIT let go: the mixer's levels again */
        ui.last = 0xFF;
    }
    hold_keys();
    if (ui.glo_held && (((released >> panel.btn[B_GLO]) & 1u) || !((fm1_in.buttons >> panel.btn[B_GLO]) & 1u)))
        glo_up();
    sys.keys_live = (uint8_t)!ui.glo_held;            /* under GLO the white keys pick, they don't play */
    sys.keys_grain = (uint8_t)(ui.view == VIEW_PAGE && ui.kind == FOCUS_DEV && ui.dev == DEV_GRAIN);
    for (k = 0; k < 27u; k++)
        if ((notes >> k) & 1u) {
            if (KEY_BLACK[k] != KEY_NONE) {
                on_black(KEY_BLACK[k]);
            } else if (ui.glo_held && KEY_WHITE[k] < NTRK) {
                sys.sel = KEY_WHITE[k];                  /* GLO + white key 1..4: the track */
                ui.glo_used = 1;
            }
        }
    for (k = 0; k < 4u; k++)
        if ((d = panel_enc(EN_K1 + k)) != 0)
            on_knob(k, d);
    if ((d = panel_enc(EN_SELECT)) != 0) {
        static const uint8_t SLOT_BTN[NSLOT] = {B_LFO, B_ENV, B_SEQ, B_ARP};
        if (ui.view == VIEW_PAGE && ui.kind == FOCUS_SLOT && ((fm1_in.buttons >> panel.btn[SLOT_BTN[ui.slot]]) & 1u)) {
            /* the focused slot's pad held + SELECT: its engine (LFO RANDOM ADSR SEQ FOLLOW), from its defaults */
            uint32_t e = (uint32_t)(((int32_t)tp[sys.sel].engine[ui.slot] + d % (int32_t)NME + (int32_t)NME) % (int32_t)NME);
            param_engine(sys.sel, ui.slot, e);
            ui.page = 0;
            ui.last = 0xFF;
        } else if (ui.view == VIEW_PAGE && ui.kind == FOCUS_DEV && ui.dev == DEV_SRC &&
                   ((fm1_in.buttons >> panel.btn[B_HOME]) & 1u)) {
            /* HOME held + SELECT: the track's source (TAPE, SYNTH); each keeps its own knobs */
            tp[sys.sel].src = (uint8_t)(((int32_t)tp[sys.sel].src + d % (int32_t)NSRC + (int32_t)NSRC) % (int32_t)NSRC);
            ui.page = 0;
            ui.last = 0xFF;
        } else {                                        /* SELECT: the global tempo */
            sys.bpm = (uint16_t)clamp((int32_t)sys.bpm + d, 40, 240);
        }
    }
    if (panel_enc(EN_PRESET))
        ui_message("PROJECTS ARRIVE IN PHASE 8");
    if (panel_enc(EN_ALGO))
        ui_message("ROUTING ARRIVES IN PHASE 6");
}

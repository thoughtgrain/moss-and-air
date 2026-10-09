/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: buttons, keys and encoders into actions, and the LEDs. Main loop only.
 *
 * Phase 1 mapping (docs/bryo-architecture.md, PRD 2):
 *   HOME           focus the track's source (TAPE); again: its page 2
 *   EDIT           focus GRAIN; again: GRAIN 2, RESONATOR, GRAIN ... (each device's pages, then the next device)
 *   FX             focus COLOR; again: COLOR 2, SPACE, SPACE 2, COLOR ...
 *   LFO ENV SEQ ARP  tapped: modulator slot 1..4's page as the pad is let go; again: its next page. Held: KNOB 1..4
 *                  set that slot's depth to the knobs on the page shown (a device, the source, the channel strip;
 *                  mod.c); held + SELECT: the slot's engine
 *   SEQ slot page  a white key held + KNOB 1: that step's value (the keys pick steps here, they don't play)
 *   MONO held      half a second: clear the focused track's modulation (SAVE held: undo)
 *   GLO held       the mixer while held: white keys 1..4 pick the track, KNOB 1..4 set the levels, SELECT sets
 *                  TRACKS; let go: back
 *   GLO tapped     the mixer stays up; tap again (or any page pad): back
 *   EDIT on the mixer  the selected track's channel (LOW HIGH FILT PAN) on KNOB 1..4: held, while held; tapped,
 *                  latched, as GLO's mixer and the 0 key's freeze. Tapped again: MASTER (the compressor: AMT ATK
 *                  REL MIX); again: the levels
 *   SCL            unassigned (the PRD's SEL; track picking moved under GLO)
 *   PLAY           start / stop
 *   REC            arm the focused track (playing: as it goes down, so a punch-in lands where it's pressed;
 *                  stopped: as it's let go). Held a second while stopped: the USB record mode (usbrec.c): REC
 *                  starts and stops the take, the white keys pick its track, REC again puts it there, HOME goes
 *                  back a step
 *   OCT- / OCT+    the white keys' octave
 *   KNOB 1..4      the four values on screen; SELECT: the tempo
 *   black OP1..OP4 track mutes
 * What the later phases add (p-locks, the views behind PRESETS, the other black keys) slots into the same
 * functions. */

static const uint8_t SLOT_BTN[NSLOT] = {B_LFO, B_ENV, B_SEQ, B_ARP};

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
    if (ui.view == VIEW_MIXER)
        return ui.chan ? B_EDIT : B_GLO;
    if (ui.view == VIEW_ROUTE || ui.view == VIEW_USBREC)   /* (no pad: ALGORITHM is a knob; REC held) */
        return NB;
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
    if (focus_btn() < NB)
        led_put(nl, panel.btn[focus_btn()], 1);
    led_put(nl, panel.btn[B_REC], ui.view == VIEW_USBREC ? ur.state == UR_RECORDING : (sys.rec >> sys.sel) & 1u);
    if (ui.view == VIEW_USBREC && ur.state == UR_CHOOSE)   /* the keys that pick the take's track */
        for (k = 0; k < 27u; k++)
            led_put(nl, 14u + k, KEY_WHITE[k] < sys.ntrk);
    led_put(nl, panel.btn[B_OCTDN], track[sys.sel].octave < 3u);
    led_put(nl, panel.btn[B_OCTUP], track[sys.sel].octave > 3u);
    for (k = 0; k < 27u; k++) {
        uint32_t b = KEY_BLACK[k];
        led_put(nl, 14u + k, (b <= BK_OP4 && track[b].mute) || (b == BK_ZERO && sys.freeze));   /* (0 lit: latched) */
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
    ui.chan_latched = 0;
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
    ui.chan_latched = 0;
    ui.glo_latched = 0;
    ui.kind = FOCUS_SLOT;
    ui.slot = (uint8_t)s;
    ui.page = again ? (uint8_t)((ui.page + 1u) % pdesc_pages(ME_P[tp[sys.sel].engine[s]])) : 0u;
    ui.last = 0xFF;
}

/* a slot's pad down: nothing yet. Let go without turning anything (slot_up), its page; held, the knobs set its
 * depths (on_knob) and SELECT its engine */
static void slot_down(uint32_t s)
{
    ui.slot_held = (uint8_t)s;
    ui.slot_used = 0;
}

static void slot_up(void)
{
    uint32_t s = ui.slot_held;
    ui.slot_held = SLOT_NONE;
    if (!ui.slot_used && s < NSLOT)
        focus_slot(s);
}

/* the target (mod.c) knob c of the page shown can be modulated as, or MOD_NTGT: none (a slot's page, the levels,
 * the master, the routing) */
static uint32_t page_target(uint32_t c)
{
    if (ui.view == VIEW_MIXER && ui.chan == CHAN_STRIP)
        return MOD_TCH + c;
    if (ui.view != VIEW_PAGE || ui.kind != FOCUS_DEV)
        return MOD_NTGT;
    c += 4u * ui.page;
    return ui.dev == DEV_SRC ? MOD_TSRC + (tp[sys.sel].src % NSRC) * NPK + c : MOD_TG(ui.dev, c);
}

/* the focused page is a SEQ slot's (the white keys pick its steps) */
static int seq_page(void)
{
    return ui.view == VIEW_PAGE && ui.kind == FOCUS_SLOT && tp[sys.sel].engine[ui.slot] == ME_SEQ;
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
    ui.chan_latched = 0;
    ui.last = 0xFF;
}

/* REC: arm the focused track (the tape made ready: a reel copied in), or let go of it */
static void rec_arm(void)
{
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
}

/* the USB record mode opens (REC held a second, stopped) */
static void usbrec_open(void)
{
    usbrec_enter();
    ui.view = VIEW_USBREC;
    ui.chan = 0;
    ui.chan_latched = 0;
    ui.glo_latched = 0;
    ui.glo_held = 0;
    ui.last = 0xFF;
}

static void on_button(uint32_t b)
{
    switch (b) {
    case B_HOME:                                        /* TAPE, its page 2 */
        pad_devices(DEV_SRC, DEV_SRC);
        break;
    case B_EDIT:                                        /* GRAIN's pages, RESONATOR's; on the mixer: the channel */
        if (ui.view == VIEW_MIXER) {                    /* (held: while held; tapped: latched; tapped again: off) */
            if (ui.chan == CHAN_MASTER && ui.chan_latched) {   /* LEVELS -> CHANNEL -> MASTER -> LEVELS */
                ui.chan = CHAN_LEVELS;
                ui.chan_latched = 0;
            } else {
                ui.chan = ui.chan == CHAN_STRIP && ui.chan_latched ? CHAN_MASTER : CHAN_STRIP;
                ui.chan_latched = 0;
                ui.chan_held = 1;
                ui.chan_used = 0;
                ui.chan_t0 = fm1_ms;
            }
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
    case B_LFO: slot_down(0); break;
    case B_ENV: slot_down(1); break;
    case B_SEQ: slot_down(2); break;
    case B_ARP: slot_down(3); break;
    case B_GLO:
        glo_down();
        break;
    case B_PLAY:
        sys.playing = (uint8_t)!sys.playing;
        break;
    case B_REC:                                         /* arm: playing, now; stopped, at the let-go (held a second
                                                         * instead: the USB record mode) */
        ui.rec_held = 1;
        ui.rec_said = 0;
        ui.rec_t0 = fm1_ms;
        if (sys.playing)
            rec_arm();
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
    } else if (k == BK_ZERO) {                          /* GRAIN's freeze: held, while it's held (the ISR reads
                                                         * the key); tapped, latched until the next tap */
        ui.zero_held = 1;
        ui.zero_t0 = fm1_ms;
    } else if (k == BK_MONO) {                          /* held half a second: clear the track's modulation */
        ui.mono_held = 1;
        ui.mono_t0 = fm1_ms;
        ui_message("KEEP HOLDING MONO TO CLEAR MODULATION");
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
            ui.undo = UNDO_TAPE;
        }
    }
    if (ui.mono_held) {                                 /* MONO: the same, for the track's modulation (mod.c) */
        for (k = 0; k < 27u && KEY_BLACK[k] != BK_MONO; k++)
            ;
        if (!((fm1_in.notes >> k) & 1u)) {
            ui.mono_held = 0;
            ui.msg_t = 1;
        } else if ((uint32_t)(fm1_ms - ui.mono_t0) >= 500u) {
            char m[40] = "T";
            ui.mono_held = 0;
            fmt_int(m + 1, (int32_t)sys.sel + 1);
            if (mod_clear(sys.sel)) {
                str_cpy(m + str_len(m), " MODULATION CLEARED. SAVE: UNDO", 32);
                ui.undo = UNDO_MOD;
            } else {
                str_cpy(m + str_len(m), " HAS NO MODULATION", 20);
            }
            ui_message(m);
        }
    }
    if (ui.zero_held) {                                 /* the 0 key let go: a tap latches the freeze or lets it go */
        for (k = 0; k < 27u && KEY_BLACK[k] != BK_ZERO; k++)
            ;
        if (!((fm1_in.notes >> k) & 1u)) {
            ui.zero_held = 0;
            if ((uint32_t)(fm1_ms - ui.zero_t0) < hold) {
                sys.freeze = (uint8_t)!sys.freeze;
                ui_message(sys.freeze ? "FROZEN. TAP 0 TO LET GO" : "FREEZE LET GO");
            }
        }
    }
    if (ui.rec_held) {                                  /* REC: a tap arms (stopped), a second's hold the record mode */
        int held = (int)((fm1_in.buttons >> panel.btn[B_REC]) & 1u);
        uint32_t ms = (uint32_t)(fm1_ms - ui.rec_t0);
        if (!held) {
            ui.rec_held = 0;
            if (!sys.playing && ms < 1000u)
                rec_arm();
            if (ui.rec_said)
                ui.msg_t = 1;                           /* (the hint ends next frame) */
        } else if (ms >= 1000u) {
            ui.rec_held = 0;
            if (sys.playing)
                ui_message("STOP, THEN HOLD REC: USB RECORD");
            else
                usbrec_open();
        } else if (ms >= 300u && !ui.rec_said && !sys.playing) {
            ui.rec_said = 1;
            ui_message("KEEP HOLDING REC: USB RECORD");
        }
    }
    if (ui.save_held) {
        int held = (int)((fm1_in.buttons >> panel.btn[B_SAVE]) & 1u);
        if (!held) {
            ui.save_held = 0;
            ui_message("SAVE: PROJECTS ARRIVE IN PHASE 8");
        } else if ((uint32_t)(fm1_ms - ui.save_t0) >= hold) {
            ui.save_held = 0;
            ui_message((ui.undo == UNDO_MOD ? mod_undo() : tape_undo_clear()) >= 0 ? "CLEAR UNDONE" : "NOTHING TO UNDO");
        }
    }
}

/* KNOB c turned by d detents: the value on screen; it lights up (hot) for half a second */
static void on_knob(uint32_t c, int32_t d)
{
    int16_t *vp;
    const pdesc_t *p = ui_page(c, &vp);
    int16_t v;
    if (ui.slot_held != SLOT_NONE && page_target(c) < MOD_NTGT) {   /* a slot's pad held: its depth to this knob */
        ui.slot_used = 1;
        if (mod_nudge(sys.sel, ui.slot_held, page_target(c), d) == -128)
            ui_message(pdesc_empty(p) ? "NO KNOB THERE" : mod_tdesc(page_target(c)) ? "32 DEPTHS ON THIS TRACK: MONO CLEARS"
                                                                                    : "THE REEL CAN'T BE MODULATED");
        ui.hot = (uint8_t)c;
        ui.hot_t = 33;
        ui.last = (uint8_t)c;
        return;
    }
    if (ui.slot_held != SLOT_NONE)                      /* (a page without depths: the knob as usual) */
        ui.slot_used = 1;
    if (seq_page() && c == 0u && ui.steps_held) {      /* a SEQ page, white keys held: KNOB 1 sets their steps */
        uint32_t k;
        for (k = 0; k < 16u; k++)
            if ((ui.steps_held >> k) & 1u)
                tp[sys.sel].steps[ui.slot][k] = (int8_t)clamp(tp[sys.sel].steps[ui.slot][k] + d, 0, 100);
        return;                                         /* (LEN isn't the knob turned: its tag stays as it was) */
    }
    if (pdesc_empty(p))                                 /* an unused knob on a page */
        return;
    v = param_nudge(p, *vp, d);
    if (ui.view == VIEW_MIXER && !ui.chan)
        track[c].level = (uint8_t)v;
    else if (ui.view == VIEW_ROUTE)
        tp[c].recin = (uint8_t)v;
    else
        *vp = v;
    ui.hot = (uint8_t)c;
    ui.hot_t = 33;
    ui.last = (uint8_t)c;
    if (ui.glo_held)
        ui.glo_used = 1;
    if (ui.chan_held)
        ui.chan_used = 1;
}

/* the USB record mode's controls (usbrec.c): REC, HOME and the white keys; the rest wait until it closes */
static void usbrec_input(uint32_t pressed, uint32_t notes)
{
    uint32_t id, k;
    sys.keys_live = 0;
    sys.keys_grain = sys.keys_reso = 0;
    for (id = 0; id < NB; id++)
        if ((pressed >> id) & 1u) {
            uint32_t b = panel_btn_of(id);
            if (b == B_REC)
                usbrec_rec();
            else if (b == B_HOME)
                usbrec_back();
        }
    for (k = 0; k < 27u; k++)
        if (((notes >> k) & 1u) && KEY_WHITE[k] != KEY_NONE)
            usbrec_pick(KEY_WHITE[k]);
    for (k = 0; k < 4u; k++) {                          /* CHOOSE: the take's START LEN GAIN FADE (otherwise turns
                                                         * are dropped, not saved up) */
        int32_t d = panel_enc(EN_K1 + k);
        if (d && ur.state == UR_CHOOSE) {
            usbrec_knob(k, d);
            ui.last = (uint8_t)k;
        }
    }
    (void)panel_enc(EN_SELECT);
    (void)panel_enc(EN_PRESET);
    (void)panel_enc(EN_ALGO);
    if (ur.state != UR_CHOOSE)
        ui.last = 0xFF;
    if (ur.state == UR_OFF) {                           /* it closed: the page it opened from */
        ui.view = VIEW_PAGE;
        ui.last = 0xFF;
        ui.force = 1;
    }
}

static void ui_input(void)
{
    uint32_t released = 0, pressed = fm1_input_edges(&released), notes = fm1_input_note_edges(), id, k;
    int32_t d;
    if (ui.view == VIEW_USBREC) {
        usbrec_input(pressed, notes);
        return;
    }
    for (id = 0; id < NB; id++)
        if ((pressed >> id) & 1u) {
            uint32_t b = panel_btn_of(id);
            if (b < NB)
                on_button(b);
        }
    if (ui.chan_held && (((released >> panel.btn[B_EDIT]) & 1u) || !((fm1_in.buttons >> panel.btn[B_EDIT]) & 1u))) {
        ui.chan_held = 0;                               /* EDIT let go: a tap keeps the channel page, a hold that
                                                         * turned something (or a long one) goes back to the levels */
        if (!ui.chan_used && (uint32_t)(fm1_ms - ui.chan_t0) < HOLD_MS[settings_hold % 4u] && ui.view == VIEW_MIXER) {
            ui.chan_latched = 1;
        } else {
            ui.chan = 0;
            ui.chan_latched = 0;
            ui.last = 0xFF;
        }
    }
    hold_keys();
    if (ui.glo_held && (((released >> panel.btn[B_GLO]) & 1u) || !((fm1_in.buttons >> panel.btn[B_GLO]) & 1u)))
        glo_up();
    ui.steps_held = (uint16_t)(seq_page() ? white_keys(fm1_in.notes) : 0u);
    sys.keys_live = (uint8_t)(!ui.glo_held && !seq_page());   /* under GLO the white keys pick the track, on a SEQ
                                                               * page the steps: they don't play */
    sys.keys_grain = (uint8_t)(ui.view == VIEW_PAGE && ui.kind == FOCUS_DEV && ui.dev == DEV_GRAIN);
    sys.keys_reso = (uint8_t)(ui.view == VIEW_PAGE && ui.kind == FOCUS_DEV && ui.dev == DEV_RESO);
    for (k = 0; k < 27u; k++)
        if ((notes >> k) & 1u) {
            if (KEY_BLACK[k] != KEY_NONE) {
                on_black(KEY_BLACK[k]);
            } else if (ui.glo_held && KEY_WHITE[k] < sys.ntrk) {
                sys.sel = KEY_WHITE[k];                  /* GLO + white key 1..TRACKS: the track */
                ui.glo_used = 1;
            }
        }
    for (k = 0; k < 4u; k++)
        if ((d = panel_enc(EN_K1 + k)) != 0)
            on_knob(k, d);
    if (ui.slot_held != SLOT_NONE && !((fm1_in.buttons >> panel.btn[SLOT_BTN[ui.slot_held]]) & 1u))
        slot_up();                                      /* (after the knobs: a turn in the same pass counts) */
    if ((d = panel_enc(EN_SELECT)) != 0) {
        if (ui.slot_held != SLOT_NONE) {
            /* a slot's pad held + SELECT: its engine (LFO ADSR SEQ FOLLOW), from its defaults; its depths stay */
            uint32_t s = ui.slot_held, e = (uint32_t)(((int32_t)tp[sys.sel].engine[s] + d % (int32_t)NME + (int32_t)NME) % (int32_t)NME);
            char m[24] = "M";
            param_engine(sys.sel, s, e);
            ui.slot_used = 1;
            if (ui.view == VIEW_PAGE && ui.kind == FOCUS_SLOT && ui.slot == s) {
                ui.page = 0;
                ui.last = 0xFF;
            } else {                                    /* (another page up: say what it is now) */
                fmt_int(m + 1, (int32_t)s + 1);
                str_cpy(m + str_len(m), " IS ", 5);
                str_cpy(m + str_len(m), ME_NAME[e], 8);
                ui_message(m);
            }
        } else if (ui.view == VIEW_PAGE && ui.kind == FOCUS_DEV && ui.dev == DEV_SRC &&
                   ((fm1_in.buttons >> panel.btn[B_HOME]) & 1u)) {
            /* HOME held + SELECT: the track's source (TAPE, SYNTH); each keeps its own knobs */
            tp[sys.sel].src = (uint8_t)(((int32_t)tp[sys.sel].src + d % (int32_t)NSRC + (int32_t)NSRC) % (int32_t)NSRC);
            ui.page = 0;
            ui.last = 0xFF;
        } else if (ui.glo_held) {                       /* GLO held + SELECT: TRACKS (how many are in use), as a pad
                                                         * held + SELECT picks that pad's thing elsewhere */
            chain_tracks((int32_t)sys.ntrk + d);
            ui.last = 0xFF;
            ui.glo_used = 1;
        } else {                                        /* SELECT: the global tempo, on every view */
            sys.bpm = (uint16_t)clamp((int32_t)sys.bpm + d, 40, 240);
        }
    }
    if (panel_enc(EN_PRESET))
        ui_message("PROJECTS ARRIVE IN PHASE 8");
    if ((d = panel_enc(EN_ALGO)) != 0) {               /* ALGORITHM: the routing view (REC IN); turned on it, the
                                                         * focused track's */
        if (ui.view != VIEW_ROUTE) {
            ui.view = VIEW_ROUTE;
            ui.chan = 0;
            ui.chan_latched = 0;
            ui.glo_latched = 0;
            ui.last = 0xFF;
        } else {
            tp[sys.sel].recin = (uint8_t)clamp((int32_t)tp[sys.sel].recin + d, 0, (int32_t)NRIN - 1);
            ui.hot = sys.sel;
            ui.hot_t = 33;
            ui.last = sys.sel;
        }
    }
}

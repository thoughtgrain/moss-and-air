/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Physical panel: which matrix button / encoder carries which printed label.
 * The matrix ids are known, the printed labels are not, so
 * the mapping is a table with a best guess, overridable by HARDWARE CALIBRATION
 * (hold OCT- and OCT+ while powering on): it asks for each label in turn (panel_setup, below).
 * The learned table lives in .noinit and, with FELUCCA_FLASH, in flash with
 * the settings (project.c); read it back with `fm1t memr` to bake it in. */
enum { B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC,
       B_OCTDN, B_OCTUP, NB };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };
static const char *const B_NAME[NB] = {"FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE",
                                        "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"};
static const char *const E_NAME[NE] = {"SELECT", "ALGORITHM", "PRESETS", "KNOB 1", "KNOB 2",
                                        "KNOB 3", "KNOB 4"};
#define PANEL_MAGIC 0x50414E35u          /* "PAN5": bump when PANEL_DEFAULT changes */

typedef struct {
    uint32_t magic;
    uint8_t btn[NB];             /* matrix button id (0..13) per label */
    uint8_t enc[NE];             /* matrix encoder (0..6) per role */
    int8_t dir[NE];              /* +1 / -1 so that clockwise is + */
} panel_t;
panel_t panel __attribute__((section(".noinit")));

static const panel_t PANEL_DEFAULT = {
    PANEL_MAGIC,
    {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1},   /* PLAY = 12, REC = 13 (measured on hardware) */
    {0, 1, 6, 2, 3, 4, 5},                           /* SELECT = enc 0, ALGORITHM = enc 1, PRESETS = enc 6 (printed labels) */
    {1, 1, 1, 1, 1, 1, 1},
};

static void panel_init(void)                     /* also after a flash load: ids are used as array indexes and shifts */
{
    uint32_t i, buttons = 0, encoders = 0, ok = panel.magic == PANEL_MAGIC;
    for (i = 0; ok && i < NB; i++) {
        ok = panel.btn[i] < NB && !(buttons & (1u << panel.btn[i]));
        if (ok) buttons |= 1u << panel.btn[i];
    }
    for (i = 0; ok && i < NE; i++) {
        ok = panel.enc[i] < NE && !(encoders & (1u << panel.enc[i])) &&
             (panel.dir[i] == 1 || panel.dir[i] == -1);
        if (ok) encoders |= 1u << panel.enc[i];
    }
    if (!ok)
        panel = PANEL_DEFAULT;
}

static uint32_t panel_btn_of(uint32_t matrix_id)        /* label of a matrix button, NB if none */
{
    uint32_t b;
    for (b = 0; b < NB; b++)
        if (panel.btn[b] == matrix_id)
            return b;
    return NB;
}

/* steps of a role, + = clockwise */
static int32_t panel_enc(uint32_t role)
{
    return fm1_enc_take(panel.enc[role]) * panel.dir[role];
}

/* Keep the retained settings layout fixed. palette: the index into UI_PALETTES (SET3 held an index
 * into the 20 palettes of earlier firmware) */
#define SETTINGS_MAGIC 0x53455434u              /* "SET4" */
#define SETTINGS_MAGIC_OLD 0x53455433u          /* "SET3" */
struct { uint32_t magic, palette, lowcut, zoom; } settings __attribute__((section(".noinit")));

static void settings_save(void);              /* settings.c: flash copy (FELUCCA_FLASH) */

/* HOLD (menu): how long a button is held before its layer opens (ui_input.c). Saved in the settings record's
 * retired bold field as HOLD_TAG | index; any other value there (0 or 1 from older firmware) is the default */
#define HOLD_TAG 0x484C4400u
#define HOLD_DEF 1u
static const uint16_t HOLD_MS[4] = {300, 400, 500, 600};
static uint8_t settings_hold = HOLD_DEF;
static uint32_t hold_from_stored(uint32_t v) { return (v & ~3u) == HOLD_TAG ? v & 3u : HOLD_DEF; }
static uint32_t hold_to_stored(uint32_t old, uint32_t i)
{
    return i % 4u != HOLD_DEF ? HOLD_TAG | (i & 3u) : old > 1u ? 0u : old;
}
static int hold_stored_ok(uint32_t v) { return v <= 1u || (v & ~3u) == HOLD_TAG; }

/* LEDS (menu): OFF, DIM LO, DIM HI, INV. DIM HI (the default), the idle buttons and keys glow dim (~1/30) and the
 * active ones are lit (#35); DIM LO the same, darker (~1/60, hal/fm1_input.h FM1_LED_DIM_LO_NS); OFF no glow,
 * the active ones lit (as 1.0); INV the idle ones lit and the active ones dark, as the stock firmware (ui_input.c
 * ui_leds). Saved in the settings record's retired zoom field as LEDS_TAG | mode, the modes append-only: DIM 0
 * (now DIM HI) and INV 1 keep their values, OFF 2 and DIM LO 3 are new; any other value there (0 or 1 from older
 * firmware) is DIM HI. LEDS_MENU: the menu's order, darkest first */
#define LEDS_TAG 0x4C454400u                    /* "LED" */
enum { LEDS_DIM, LEDS_INV, LEDS_OFF, LEDS_DIM_LO, LEDS_COUNT };
static const uint8_t LEDS_MENU[LEDS_COUNT] = {LEDS_OFF, LEDS_DIM_LO, LEDS_DIM, LEDS_INV};
static const char *const LEDS_NAME[LEDS_COUNT] = {"DIM HI", "INV", "OFF", "DIM LO"};
static uint8_t settings_leds = LEDS_DIM;
static uint32_t leds_from_stored(uint32_t v) { return (v & ~3u) == LEDS_TAG && (v & 3u) < LEDS_COUNT ? v & 3u : LEDS_DIM; }
static uint32_t leds_to_stored(uint32_t old, uint32_t m)
{
    return m != LEDS_DIM && m < LEDS_COUNT ? LEDS_TAG | m : old > 1u ? 0u : old;
}
static int leds_stored_ok(uint32_t v) { return v <= 1u || ((v & ~3u) == LEDS_TAG && (v & 3u) < LEDS_COUNT); }
/* the menu's next / previous mode (KNOB 1 stops at the ends, OCT+ (s 0) cycles) */
static uint32_t leds_step(uint32_t m, int32_t s)
{
    uint32_t i = 0;
    while (i + 1u < LEDS_COUNT && LEDS_MENU[i] != m)
        i++;
    i = s > 0 ? (i + 1u < LEDS_COUNT ? i + 1u : i) : s < 0 ? (i ? i - 1u : 0u) : (i + 1u) % LEDS_COUNT;
    return LEDS_MENU[i];
}

static void settings_init(void)
{
    if (settings.magic == SETTINGS_MAGIC_OLD && settings.palette < 20u) {
        settings.magic = SETTINGS_MAGIC;
        settings.palette = palette_from_stored(settings.palette);
    }
    if (settings.magic != SETTINGS_MAGIC || settings.palette >= NPALETTES) {
        settings.magic = SETTINGS_MAGIC;
        settings.palette = UI_GREY_INDEX;      /* GREY (default; named MONO before 1.0.2) */
        settings.lowcut = 0;
        settings.zoom = 0;                     /* (retired: the LEDS setting, settings_persist.c) */
    }
    palette_set(settings.palette);
    fx_lowcut = (uint8_t)(settings.lowcut % 3u);
}

/* ------------------------------------------------- HARDWARE CALIBRATION --- */
/* OCT- + OCT+ held at power-on (main.c): press each button and turn each encoder as asked; the learned table is
 * saved with the settings. 30 s without input cancels and keeps the old table. */
#define SETUP_IDLE_MS 30000u
#define SETUP_HEAD_MY CAP_IN(M, 24)              /* the title in a 24 px band, as Felucca drew it */
static void setup_title(void)
{
    lcd_fill(0, 0, 240, 240, T_BG);
    {   /* the title with its icon (the menu row's), centred together; M from y 8 as before */
        const char *t = "HARDWARE CALIBRATION";
        int32_t x = (240 - (16 + 6 + text_w(&AF_M, t))) / 2;
        cv_begin(240, 24, T_BG);
        GFX_HOOK_ALIGN(0, 0, 240, 0, AL_H | AL_N(2), "calibration title centred");
        GFX_HOOK_ALIGN(0, SETUP_HEAD_MY + AF_M_CAP_Y, 0, SETUP_HEAD_MY + AF_M_CAP_Y + AF_M_CAP_H, AL_V | AL_PASS,
                       "header icon on its title's line");
        cv_icon_mid(x, 12, 16, ICON_X_DOCTOR, T_THEME, T_BG);
        cv_text(x + 22, SETUP_HEAD_MY, &AF_M, t, T_TEXT);
        cv_blit(0, 5);
    }
    draw_text_box(0, 32, 240, &AF_S, "TEACH EACH BUTTON AND KNOB", T_MID, 1);
    lcd_fill(16, 56, 208, 1, T_LINE);
}
static void setup_show(const char *what, const char *name)     /* "PRESS" / "TURN RIGHT", the control */
{
    draw_text_box(0, 80, 240, &AF_S, what, T_MID, 1);
    draw_text_box(0, 100, 240, &AF_L, name, T_THEME, 1);
}
static void panel_setup(void)
{
    uint32_t i, used = 0, t0 = fm1_ms;
    const panel_t old = panel;
    setup_title();
    while (fm1_in.buttons) {                             /* wait for OCT-/OCT+ release */
        fm1_wdt_feed();
        if (fm1_ms - t0 > SETUP_IDLE_MS)
            goto timeout;
    }
    fm1_input_edges(0);
    for (i = 0; i < NB; i++) {
        uint32_t p = 0, id;
        setup_show("PRESS", B_NAME[i]);
        t0 = fm1_ms;
        while (!(p & ~used)) {
            fm1_wdt_feed();
            p |= fm1_input_edges(0);
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
        }
        for (id = 0; id < 14u; id++)
            if (((p & ~used) >> id) & 1u)
                break;
        panel.btn[i] = (uint8_t)id;
        used |= 1u << id;
    }
    used = 0;
    for (i = 0; i < NE; i++) {
        uint32_t e;
        int32_t st = 0;
        setup_show("TURN RIGHT", E_NAME[i]);
        for (e = 0; e < 7u; e++)
            fm1_enc_take(e);
        t0 = fm1_ms;
        for (;;) {
            fm1_wdt_feed();
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
            for (e = 0; e < 7u; e++)
                if (!((used >> e) & 1u) && (st = fm1_enc_take(e)) != 0)
                    break;
            if (e < 7u)
                break;
        }
        panel.enc[i] = (uint8_t)e;
        panel.dir[i] = (int8_t)(st > 0 ? 1 : -1);
        used |= 1u << e;
        fm1_delay_ms(300);
        fm1_enc_take(e);
    }
    panel.magic = PANEL_MAGIC;
    lcd_fill(0, 0, 240, 240, T_BG);
    ui_redraw();
    return;
timeout:
    panel = old;
    lcd_fill(0, 0, 240, 240, T_BG);
    ui_redraw();
    ui_message("SETUP CANCELLED");
}

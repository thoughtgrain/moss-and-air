/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Bryo: the settings record in flash (storage.c OBJ_SETTINGS): the palette, the speaker EQ, HOLD, LEDS and the
 * learned panel table (HARDWARE CALIBRATION). The layout is Felucca's PER4 (its settings_persist.c), kept on
 * purpose: installing Bryo over Felucca keeps the calibration and the palette, and the fields only Felucca uses
 * (its favorites, its MENU flags) are carried through untouched, so going back to Felucca loses nothing.
 * Older records (PER1..PER3) are read as Felucca read them. */
typedef struct {
    uint32_t magic, palette, lowcut, zoom;
    panel_t panel;
    uint32_t bold;
    uint8_t felucca[16 * 32 + 8];   /* Felucca's favorites (struct { factory[16][32]; user, filter; }): opaque */
} persist_t;
#define PERSIST_MAGIC 0x50455234u   /* "PER4" */

/* Felucca's MENU flags (its favorites.factory[15][30]): Bryo reads and keeps the two that are about the USB link,
 * so a setting made under Felucca still holds (macOS 13-15 need USB SERIAL OFF to see the USB audio). */
#define PREF_USB_FIXED 8u            /* MENU > USB LEVEL FIXED: USB audio at the full level, MASTER after */
#define PREF_SERIAL_OFF 64u          /* MENU > USB SERIAL OFF: no serial console, audio + MIDI only */
#define PREFS_AT (15u * 32u + 30u)
static uint8_t ui_prefs;

/* Normalize in place; 1 = current, 2 = migrated, 0 = invalid. */
static int settings_import(persist_t *p, int n)
{
    int current = n == (int)sizeof *p && p->magic == PERSIST_MAGIC;
    int old3 = n == (int)(sizeof *p - sizeof p->felucca) && p->magic == 0x50455233u;
    int old2 = n == (int)(sizeof *p - sizeof p->felucca - sizeof p->bold) && p->magic == 0x50455232u;
    int old1 = n == (int)(8u + sizeof(panel_t)) && p->magic == 0x50455231u;
    if (!(current || old3 || old2 || old1))
        return 0;
    if (old1) {
        panel_t old;
        memcpy(&old, (uint8_t *)p + 8, sizeof old);
        p->panel = old;
        p->lowcut = p->zoom = 0;
    }
    if (old1 || old2)
        p->bold = 0;
    if (!current)
        memset(p->felucca, 0, sizeof p->felucca);
    p->magic = PERSIST_MAGIC;
    p->palette = palette_to_stored(palette_from_stored(p->palette));
    settings.magic = SETTINGS_MAGIC;
    settings.palette = palette_from_stored(p->palette);
    settings.lowcut = p->lowcut;
    settings.zoom = p->zoom;
    settings_hold = (uint8_t)hold_from_stored(p->bold);
    settings_leds = (uint8_t)leds_from_stored(p->zoom);
    if (p->panel.magic == PANEL_MAGIC)
        panel = p->panel;
    ui_prefs = p->felucca[PREFS_AT];
    fx_usb_fixed = (uint8_t)((ui_prefs & PREF_USB_FIXED) != 0u);
    return current ? 1 : 2;
}

/* Start from the last record read or saved, so the fields Bryo doesn't own keep their values. */
static void settings_export(persist_t *p)
{
    p->magic = PERSIST_MAGIC;
    p->palette = palette_to_stored(settings.palette);
    p->lowcut = settings.lowcut;
    p->zoom = settings.zoom = leds_to_stored(settings.zoom, settings_leds);
    p->panel = panel;
    p->bold = hold_to_stored(p->bold, settings_hold);
    p->felucca[PREFS_AT] = ui_prefs;
}

/* MENU > USB SERIAL (Felucca #67): ON presents the serial console; OFF enumerates as audio + MIDI only. Applied at
 * boot, before USB starts (main.c). */
static void usb_serial_apply(void)
{
#if FELUCCA_CDC
    usb_cdc_switch(FELUCCA_CDC_DEFAULT && !(ui_prefs & PREF_SERIAL_OFF));
#endif
}

#if FELUCCA_FLASH
static persist_t persist_saved;
static uint8_t persist_pending;                 /* 1 requested, 2 waiting after a flash error */
static uint32_t persist_retry_ms;
#endif

static void persist_boot(void)                  /* before settings_init / panel_init */
{
#if FELUCCA_FLASH
    persist_t p;
    uint32_t f = irq_save();
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;   /* the expected 1 MiB part, else stay RAM-only */
    irq_restore(f);
    if (!flash_ok)
        return;
    fl_plain_window_init();                     /* flash above 0x93000 reads as plaintext through XIP (the reels) */
    {
        int n = st_load(OBJ_SETTINGS, &p, sizeof p);
        if (settings_import(&p, n))
            persist_saved = p;
    }
#endif
}

/* Main loop: a queued save, only while stopped (a flash write blocks interrupts: the audio drops out). */
static void settings_poll(void)
{
#if FELUCCA_FLASH
    persist_t p;
    if (!persist_pending || !flash_ok || sys.playing ||
        (persist_pending == 2u && (uint32_t)(fm1_ms - persist_retry_ms) < 1000u))
        return;
    p = persist_saved;
    settings_export(&p);
    if (!memcmp(&p, &persist_saved, sizeof p)) {
        persist_pending = 0;
        return;                                 /* unchanged: no erase cycle */
    }
    if (st_save(OBJ_SETTINGS, &p, sizeof p) == 0) {
        persist_saved = p;
        persist_pending = 0;
    } else {
        persist_pending = 2;
        persist_retry_ms = fm1_ms;
    }
#endif
}

static void settings_save(void)
{
#if FELUCCA_FLASH
    persist_pending = 1;
#endif
    settings_poll();
}

#if FELUCCA_FLASH
_Static_assert(sizeof(persist_t) <= ST_PAYLOAD_MAX, "settings do not fit one flash sector");
_Static_assert(sizeof(persist_t) == 4u * 4u + sizeof(panel_t) + 4u + 16u * 32u + 8u,
               "the record must stay Felucca's PER4 layout (its size is how a reader tells PER4 from PER3)");
#endif

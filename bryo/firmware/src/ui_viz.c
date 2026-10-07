/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: the visualization panel under the 4-value strip (y 124..215). One picture per view, drawn from the
 * parameter values (not from audio), so it is right the moment a knob turns and costs nothing while nothing
 * changes.
 *
 * The look: bold and iconographic, the way groovebox screens read at arm's length. Each panel has a caption bar
 * (the device's pictogram and name at the left; at the right, the last-turned knob as a solid tag: its pictogram,
 * label and value), then one picture made of solid shapes over a dotted grid: filled areas with a bright 2 px edge,
 * blocks for discrete things (steps, echoes, grains), small tags for names. Nothing is a hairline.
 *
 *   TAPE       a tape strip with its sprocket holes; the loop window as a bracketed block (START, LENGTH), the
 *              playhead a triangle, SPEED as chevrons, DUB as stacked layers
 *   GRAIN      the sample in TAPE's loop window, lit where grains read it (SIZE, scaled by PITCH), and one solid
 *              block per grain (DENS) in a stereo lane under it (SPREAD, up = left, down = right)
 *   RESONATOR  the filled response over 8 octaves: peaks on ROOT's harmonics (dots on the floor, the root tagged),
 *              as sharp as FDBK makes them, rolling off with DAMP, blended with the dotted dry line by MIX
 *   COLOR      a filled sine through the device; the inset follows the last-turned knob: the transfer curve
 *              (DRIVE, CRUSH), the noise riding the envelope (NOISE) or the tone filter (TONE)
 *   SPACE      the dry hit, the echoes as bars (TIME apart, falling by FDBK) and the filled reverb tail (SIZE, DECAY)
 *   MOD slots  in the slot's colour: WAVE filled, RANDOM as stepped bars, ADSR filled with its A D S R tags, SEQ as
 *              16 step blocks in four groups
 *   MIXER      the four tracks: level, meter, mute, which one is focused
 *
 * The last-turned knob (ui.last) is drawn in the accent where the picture shows it, and named in the tag. A
 * modulator's shape keeps its slot's colour (PRD 5: the colours are the slots' identity). The DSP of each device
 * will use the same mappings as these pictures (the comments name them), so what is drawn is what is heard.
 * Integer only: no float on the device. */

#define VZ_H 92
#define VX0 8
#define VX1 232
#define VW (VX1 - VX0)
#define VY0 26                       /* the plot, below the caption bar */
#define VY1 86
#define VH (VY1 - VY0)
#define VMID ((VY0 + VY1) / 2)

static uint32_t vz_seed;
static uint32_t vz_rand(void)        /* deterministic: the same picture for the same values */
{
    vz_seed = vz_seed * 1664525u + 1013904223u;
    return vz_seed >> 8;
}

static uint16_t vz_hi(int focus, uint16_t c) { return focus ? T_ACCENT : c; }
static uint16_t vz_fill(uint16_t c) { return ux_mix(c, T_BG, 32); }   /* a shape's body under its bright edge */

/* a vertical run, 1 px wide, either way round */
static void vz_vline(int32_t x, int32_t y0, int32_t y1, uint16_t c)
{
    if (y1 < y0) {
        int32_t t = y0;
        y0 = y1;
        y1 = t;
    }
    cv_rect(x, y0, 1, y1 - y0 + 1, c);
}

/* the dotted grid: a dot every 4 px on the rows given */
static void vz_dots(int32_t y, uint16_t c)
{
    int32_t x;
    for (x = VX0; x <= VX1; x += 4)
        cv_rect(x, y, 1, 1, c);
}

/* a small tag: text on a solid rounded block; returns its width */
static int32_t vz_tag(int32_t x, int32_t y, const char *s, uint16_t fill, uint16_t ink)
{
    int32_t w = text_w(&AF_S, s) + 8;
    cv_rrect(x, y, w, 15, 3, fill, T_BG);
    cv_text_on(x + 4, y + 1, &AF_S, s, ink, fill);
    return w;
}

/* each knob's pictogram, for the tag (the devices, then the modulator engines, then the mixer's levels) */
static const uint8_t DEV_ICON[NDEV][4] = {
    {ICON_PHASE, ICON_LENGTH, ICON_RATE, ICON_X_PLUS},           /* TAPE: START LENGTH SPEED DUB (12 px: no REC) */
    {ICON_SIZE, ICON_GRAIN, ICON_PITCH, ICON_PAN},               /* GRAIN: SIZE DENS PITCH SPREAD */
    {ICON_TUNE, ICON_FEEDBACK, ICON_DAMP, ICON_MIX},             /* RESONATOR: ROOT FDBK DAMP MIX */
    {ICON_DRIVE, ICON_BITS, ICON_NOISE, ICON_TONE},              /* COLOR: DRIVE CRUSH NOISE TONE */
    {ICON_TIME, ICON_FEEDBACK, ICON_SIZE, ICON_DECAY},           /* SPACE: TIME FDBK SIZE DECAY */
};
static const uint8_t ME_ICON[NME][4] = {
    {ICON_RATE, ICON_SHAPE, ICON_FOLD, ICON_PHASE},              /* WAVE: RATE SHAPE FOLD SKEW */
    {ICON_RATE, ICON_GLIDE, ICON_LEVEL, ICON_X_UP},              /* RANDOM: RATE SMOOTH SPREAD BIAS */
    {ICON_ATTACK, ICON_DECAY, ICON_SUSTAIN, ICON_RELEASE},       /* ADSR */
    {ICON_STEPS, ICON_DIVISION, ICON_SLIDE, ICON_SWING},         /* SEQ: STEPS RATE SLEW SWING */
};

/* the caption bar: the view's pictogram and name (left); the last-turned knob as a solid tag (right) */
static void vz_head(uint32_t icon, const char *name, uint16_t ic)
{
    int32_t right = VX1;                                                /* the caption stops short of the tag */
    cv_icon_mid(VX0, 10, 16, icon, ic, T_BG);
    if (ui.last < 4u && ui.view == VIEW_PAGE) {
        int16_t *vp;
        const pdesc_t *d = ui_page(ui.last, &vp);
        char b[24], val[12];
        const char *unit;
        uint32_t kic = ui.kind == FOCUS_SLOT ? ME_ICON[tp[sys.sel].engine[ui.slot]][ui.last] : DEV_ICON[ui.dev][ui.last];
        int32_t w;
        param_format(d, *vp, val, &unit);
        str_cpy(b, d->label, sizeof b);
        str_cpy(b + str_len(b), " ", 2);
        str_cpy(b + str_len(b), val, 12);
        str_cpy(b + str_len(b), unit, 4);
        w = 5 + 12 + 4 + text_w(&AF_S, b) + 6;
        right = VX1 - w - 6;
        cv_rrect(VX1 - w, 1, w, 18, 4, T_ACCENT, T_BG);
        cv_icon_mid(VX1 - w + 5, 10, 12, kic, T_BG, T_ACCENT);
        cv_text_on(VX1 - w + 5 + 12 + 4, 3, &AF_S, b, T_BG, T_ACCENT);
    }
    cv_text_fit(VX0 + 21, 3, &AF_S, name, T_MID, T_BG, right - VX0 - 21);
}

/* -------------------------------------------------------------- TAPE --- */
static void viz_tape(const int16_t *v, uint32_t f)
{
    int32_t sy = VY0 + 10, sh = 30, x0 = VX0 + v[0] * VW / 100, x1 = x0 + v[1] * VW / 100, sp = v[2], i, n;
    if (x1 > VX1)
        x1 = VX1;
    vz_head(ICON_TAPE, "TAPE  EMPTY", T_THEME);                        /* (phase 2: the recorded wave on the tape) */
    cv_rrect(VX0, sy, VW, sh, 4, T_RAISE, T_BG);                          /* the tape */
    cv_rect(x0, sy, x1 - x0, sh, vz_fill(vz_hi(f <= 1u && ui.last < 4u, T_THEME)));   /* the loop window */
    for (i = VX0 + 6; i < VX1 - 4; i += 9) {                              /* sprocket holes, both edges */
        cv_rect(i, sy + 3, 4, 3, T_BG);
        cv_rect(i, sy + sh - 6, 4, 3, T_BG);
    }
    {   /* the brackets: START and the end (LENGTH), with feet */
        uint16_t cs = vz_hi(f == 0u, T_THEME), ce = vz_hi(f == 1u, T_THEME);
        cv_rect(x0, sy - 5, 3, sh + 10, cs);
        cv_rect(x0, sy - 5, 7, 3, cs);
        cv_rect(x0, sy + sh + 2, 7, 3, cs);
        cv_rect(x1 - 3, sy - 5, 3, sh + 10, ce);
        cv_rect(x1 - 7, sy - 5, 7, 3, ce);
        cv_rect(x1 - 7, sy + sh + 2, 7, 3, ce);
    }
    {   /* the playhead: a solid triangle over the tape where the loop starts in the playing direction */
        int32_t px = sp < 0 ? x1 - 10 : x0 + 6, k;
        for (k = 0; k < 5; k++)
            cv_rect(px + k, sy - 9 + k, 9 - 2 * k, 1, T_TEXT);
    }
    n = sp < 0 ? -sp : sp;                                                /* SPEED: chevrons, one per 50 % */
    n = n == 0 ? 0 : clamp(n / 50 + 1, 1, 4);
    {
        int32_t cx = (x0 + x1) / 2 - n * 6, cy = sy + sh / 2;
        uint16_t c = vz_hi(f == 2u, T_TEXT);
        if (!n) {                                                         /* stopped: a pause sign */
            cv_rect((x0 + x1) / 2 - 5, cy - 7, 4, 14, c);
            cv_rect((x0 + x1) / 2 + 2, cy - 7, 4, 14, c);
        }
        for (i = 0; i < n; i++) {
            int32_t x = cx + i * 12, d = sp < 0 ? -1 : 1, b = sp < 0 ? x + 7 : x;
            cv_line_t(b, cy - 7, b + 7 * d, cy, c, 3);
            cv_line_t(b, cy + 7, b + 7 * d, cy, c, 3);
        }
    }
    {   /* DUB: three stacked layers under the window, as many lit as DUB keeps of the old take */
        int32_t lit = (v[3] + 33) / 34, k, w = 26, lx = (x0 + x1) / 2 - w / 2;
        for (k = 0; k < 3; k++)
            cv_rrect(lx + k * 2, VY1 - 2 - k * 4, w - k * 4, 3, 1, k < lit ? vz_hi(f == 3u, T_THEME) : T_RAISE, T_BG);
    }
}

/* ------------------------------------------------------------- GRAIN --- */
/* The sample under the grains: its envelope at position pos (0..1000 of the tape), 0..1000. Until TAPE records
 * (phase 2) it is a demo: a loop of eight decaying hits with some grit, and the caption says DEMO; phase 2 reads
 * the track's tape here instead, and the drawing stays as it is. */
#define TAPE_MS 3300                 /* a tape's length (docs/bryo-architecture.md: 36 KiB of ADPCM at 22.05 kHz) */
static int32_t tape_env(uint32_t t, int32_t pos)
{
    int32_t d = pos % 125, e = d < 50 ? 1000 - d * 18 : 100 - (d - 50);   /* a hit every 1/8, ~50 ms decay */
    uint32_t h = (uint32_t)pos * 2654435761u + t * 97u;
    return clamp(e + (int32_t)(h >> 26) * 4 - 120, 30, 1000);
}

static void viz_grain(const int16_t *v, uint32_t f)
{
    /* The view spans TAPE's loop window (START, LENGTH). DENS 0..100: 1..25 grains, placed along the loop. Each
     * grain reads SIZE ms of the sample, times 2^(PITCH/12) (pitched up, it reads more), so that is the slice of
     * the waveform it lights and the width of its block. SPREAD scatters the blocks across the stereo lane under
     * the waveform (up = left). Solid squares: the grains as the engine schedules them, without the window shape. */
    const int16_t *tv = tp[sys.sel].dev[DEV_SRC];
    int32_t n = 1 + v[1] * 24 / 100, i, x, g;
    int32_t start = tv[0] * 10, len = tv[1] * 10;                      /* the loop in 1/1000 of the tape */
    int32_t loop_ms = len * TAPE_MS / 1000, oct = 1000, span;
    int32_t gx[25], gy[25], wmid = VY0 + 16, wh = 15, lane = VY1 - 7, gw = VW - 10;   /* L and R at the right */
    for (i = 0; i < v[2]; i++) oct = oct * 1059 / 1000;                 /* 2^(st/12), 1/1000 */
    for (i = 0; i > v[2]; i--) oct = oct * 1000 / 1059;
    span = v[0] * oct / 1000 * gw / (loop_ms > 0 ? loop_ms : 1);        /* the slice each grain reads, px */
    span = clamp(span, 3, gw / 2);
    vz_head(ICON_GRAIN, "GRAINS  DEMO", T_THEME);                       /* (phase 2: "GRAINS  TAPE") */
    vz_seed = 12345u;
    for (g = 0; g < n; g++) {
        gx[g] = VX0 + (int32_t)(vz_rand() % (uint32_t)(gw - span));
        gy[g] = ((int32_t)(vz_rand() % 2001u) - 1000) * v[3] / 100;     /* -1000 (L) .. 1000 (R) */
    }
    /* the waveform: dim, and bright where a grain reads it */
    for (x = VX0; x < VX0 + gw; x++) {
        int32_t a = tape_env(sys.sel, start + (x - VX0) * len / gw) * wh / 1000, lit = 0;
        for (g = 0; g < n && !lit; g++)
            lit = x >= gx[g] && x < gx[g] + span;
        vz_vline(x, wmid - a, wmid + a, lit ? vz_hi(f == 0u || f == 2u, T_TEXT) : T_RAISE);
    }
    /* the stereo lane and the grains in it: one solid block per grain under the slice it plays */
    vz_dots(lane, vz_hi(f == 3u, T_MID));
    cv_text_on(VX1 - 6, lane - 17, &AF_S, "L", T_DIM, T_BG);           /* up = left, down = right */
    cv_text_on(VX1 - 6, lane + 2, &AF_S, "R", T_DIM, T_BG);
    for (g = 0; g < n; g++)
        cv_rect(gx[g], lane - 3 + gy[g] * 6 / 1000, span, 6, vz_hi(f == 0u || f == 2u, T_THEME));
    if (f == 1u) {                                                     /* DENS: the count, as a tag */
        char b[16];
        fmt_int(b, n);
        str_cpy(b + str_len(b), " GRAINS", 8);
        vz_tag(VX1 - 4 - text_w(&AF_S, b) - 8, VY0 + 32, b, T_ACCENT, T_BG);
    }
}

/* --------------------------------------------------------- RESONATOR --- */
/* 12 log2(k) in 1/16 semitones: harmonic k's distance above the root */
static const int16_t HARM_16[24] = {0, 192, 304, 384, 446, 496, 539, 576, 609, 638, 664, 688,
                                    710, 731, 750, 768, 785, 801, 816, 830, 843, 856, 869, 880};
static void viz_reso(const int16_t *v, uint32_t f)
{
    /* x: MIDI 28 (E1, 41 Hz) .. 124 (8 octaves) in 1/16 semitones; peaks at ROOT's harmonics; the peak width
     * narrows as FDBK rises; DAMP rolls the upper harmonics off; MIX blends with the flat dry line */
    int32_t x, py = VY1, root16 = v[0] * 16, width16 = 4 + (100 - v[1]) * 28 / 100, k;
    int32_t dry = VY1 - VH * 35 / 100, base = VY1 - 4;
    uint16_t edge = vz_hi(f == 1u || f == 2u, T_THEME);
    vz_head(ICON_RESO, "RESONATOR", T_THEME);
    vz_dots(dry, vz_hi(f == 3u, T_MID));                                /* the dry level */
    for (x = VX0; x <= VX1; x++) {
        int32_t n16 = 28 * 16 + (x - VX0) * 96 * 16 / VW, best = 0, y;
        for (k = 0; k < 24; k++) {
            int32_t d = n16 - (root16 + HARM_16[k]), amp, pk;
            if (d < -width16 * 6 || d > width16 * 6)
                continue;
            amp = 1000 - k * v[2] * 9;                                  /* DAMP: the higher harmonics lower */
            if (amp <= 0)
                break;
            d = d < 0 ? -d : d;
            pk = amp * width16 * width16 / (width16 * width16 + d * d); /* 1 / (1 + (d / w)^2) */
            if (pk > best)
                best = pk;
        }
        best = (best * v[3] + 350 * (100 - v[3])) / 100;                /* MIX with the dry line (35 %) */
        y = base - best * (base - VY0) / 1000;
        vz_vline(x, y, base, vz_fill(edge));                            /* the body */
        if (x > VX0)
            cv_line_t(x - 1, py, x, y, edge, 2);                        /* the edge */
        py = y;
    }
    for (k = 0; k < 24; k++) {                                          /* the harmonics: dots on the floor */
        int32_t hx = VX0 + (root16 + HARM_16[k] - 28 * 16) * VW / (96 * 16);
        if (hx > VX1 - 3)
            break;
        if (1000 - k * v[2] * 9 > 0)
            cv_rect(hx - 1, VY1 - 1, 3, 3, k ? T_MID : vz_hi(f == 0u, T_TEXT));
    }
    {   /* ROOT: its note, tagged over its peak */
        int32_t rx = VX0 + (root16 - 28 * 16) * VW / (96 * 16);
        char b[8];
        const char *u;
        param_format(&DEV_P[DEV_RESO][0], v[0], b, &u);
        vz_tag(clamp(rx - 10, VX0, VX1 - 30), VY0 - 2, b, vz_hi(f == 0u, T_THEME), T_BG);
    }
}

/* ------------------------------------------------------------- COLOR --- */
/* DRIVE 0..100 -> gain 1..16 into the soft clip; CRUSH 0..100 -> 16..2 bits and a hold of 1..12 px (the rate);
 * NOISE: noise riding the signal's envelope; TONE: a one-pole low-pass on that noise */
static int32_t color_shape(int32_t x, const int16_t *v)             /* Q15 in -> Q15 out */
{
    int32_t g = 1 + v[0] * 15 / 100, y = softclip(x * g) * 32767 / softclip(32767 * g);
    int32_t bits = 16 - v[1] * 14 / 100, q = 1 << (16 - bits);
    return q > 1 ? (y / q) * q : y;
}

static void viz_color(const int16_t *v, uint32_t f)
{
    static const uint8_t HEAD_ICON[4] = {ICON_DRIVE, ICON_BITS, ICON_NOISE, ICON_TONE};
    static const char *const HEAD_NAME[4] = {"DRIVE", "CRUSH", "NOISE", "TONE"};
    int32_t bx = VX0, bw = 62, wx = VX0 + 72, ww = VX1 - wx, i, py = VMID, held = 0, hold = 1 + v[1] * 11 / 100;
    int32_t lp = 0, a = 3 * (100 - v[3]) / 4 + 8;                       /* TONE: the low-pass step, 8..83 / 100 */
    uint16_t ec = vz_hi(f <= 1u && ui.last < 4u, T_THEME);
    vz_head(f < 4u ? HEAD_ICON[f] : ICON_DRIVE, f < 4u ? HEAD_NAME[f] : "COLOR", T_THEME);
    /* the inset: the transfer curve, or (TONE) the tone filter's response, filled */
    cv_rrect(bx, VY0, bw, VH, 4, T_RAISE, T_BG);
    if (f == 3u) {
        int32_t pyc = VY1 - 4;
        for (i = 0; i < bw - 6; i++) {
            int32_t fr = i * 100 / (bw - 6), g = 100 * a / (a + fr + 1), y = VY1 - 4 - g * (VH - 10) / 100;
            vz_vline(bx + 3 + i, y, VY1 - 4, vz_fill(T_ACCENT));
            if (i)
                cv_line_t(bx + 2 + i, pyc, bx + 3 + i, y, T_ACCENT, 2);
            pyc = y;
        }
    } else {
        int32_t pyc = VMID;
        cv_rect(bx + 4, VMID, bw - 8, 1, T_BG);
        for (i = 0; i < bw - 8; i++) {
            int32_t x = (i * 2 - (bw - 8)) * 32767 / (bw - 8), y = color_shape(x, v);
            int32_t yy = VMID - y * (VH / 2 - 5) / 32767;
            if (i)
                cv_line_t(bx + 3 + i, pyc, bx + 4 + i, yy, ec, 2);
            pyc = yy;
        }
    }
    /* the waveform: two cycles of a sine through the device, filled to the middle */
    for (i = 0; i < ww; i++) {
        int32_t s = sine_i((uint32_t)i * (0xFFFFFFFFu / (uint32_t)ww) * 2u), y, nz, env;
        if (i == 0)
            vz_seed = 777u;
        if (i % hold == 0)
            held = color_shape(s, v);                                  /* CRUSH's rate: hold a value */
        env = s < 0 ? -s : s;
        nz = ((int32_t)(vz_rand() & 0xFFFF) - 32768) * v[2] / 100;      /* NOISE, before TONE */
        lp += (nz - lp) * a / 100;
        y = held + (lp * env >> 16);                                   /* (|lp| <= 32768, env <= 32767: fits) */
        y = VMID - clamp(y, -32767, 32767) * (VH / 2 - 2) / 32767;
        vz_vline(wx + i, y, VMID, vz_fill(f == 2u ? T_ACCENT : T_TEXT));
        if (i)
            cv_line_t(wx + i - 1, py, wx + i, y, f == 2u ? T_ACCENT : T_TEXT, 2);
        py = y;
    }
    vz_dots(VMID, T_MID);
}

/* ------------------------------------------------------------- SPACE --- */
/* the window is 2 s: the dry hit at 0, echoes TIME ms apart falling by FDBK, the tail rising over SIZE
 * (10..90 ms) and falling over DECAY (0.2..4.2 s) */
static void viz_space(const int16_t *v, uint32_t f)
{
    int32_t t, x, py = VY1, rise = 10 + v[2] * 80 / 100, len = 200 + v[3] * 40, g = 32767;
    uint16_t tc = f >= 2u && ui.last < 4u ? T_ACCENT : T_THEME, ec = vz_hi(f <= 1u && ui.last < 4u, T_TEXT);
    vz_head(f <= 1u && ui.last < 4u ? ICON_DELAY : ICON_REVERB, "SPACE", T_THEME);
    for (x = VX0; x <= VX1; x++) {                                      /* the tail, filled */
        int32_t ms = (x - VX0) * 2000 / VW, e;
        if (ms < rise)
            e = ms * 1000 / rise;
        else
            e = 1000 - (ms - rise) * 1000 / len;
        if (e <= 0)
            continue;
        e = e * e / 1000 * 55 / 100;                                    /* exponential-ish, 55 % of the height */
        vz_vline(x, VY1, VY1 - e * VH / 1000, vz_fill(tc));
        if (x > VX0)
            cv_line_t(x - 1, py, x, VY1 - e * VH / 1000, tc, 2);
        py = VY1 - e * VH / 1000;
    }
    cv_rrect(VX0, VY0, 5, VH, 2, T_TEXT, T_BG);                         /* the dry hit */
    for (t = v[0]; t < 2000 && g > 300; t += v[0]) {                    /* the echoes: bars */
        int32_t h;
        g = (t == v[0]) ? 26000 : g * v[1] / 100;
        x = VX0 + t * VW / 2000;
        h = g * VH / 32767;
        if (h >= 3)
            cv_rrect(x - 2, VY1 - h, 5, h, 2, ec, T_BG);
    }
    vz_dots(VY1, T_MID);
}

/* ------------------------------------------------------------- MODS --- */
static void viz_wave(const int16_t *v, uint16_t c, uint32_t f)
{
    /* SHAPE 0 sine, 1 triangle, 2 square; SKEW moves the peak (-100..100); FOLD folds the top back (0..100) */
    static const uint8_t SHAPE_ICON[3] = {ICON_W_SIN, ICON_W_TRI, ICON_W_SQR};
    static const char *const SHAPE_NAME[3] = {"WAVE  SINE", "WAVE  TRIANGLE", "WAVE  SQUARE"};
    int32_t i, py = VMID, sk = clamp(500 + v[3] * 5, 50, 950), sh = clamp(v[1], 0, 2);   /* the peak's place */
    (void)f;
    vz_head(SHAPE_ICON[sh], SHAPE_NAME[sh], c);
    vz_dots(VMID, T_MID);
    for (i = 0; i <= VW; i++) {
        int32_t p = (i * 2000 / VW) % 1000, q, y;                       /* two cycles */
        q = p < sk ? p * 500 / sk : 500 + (p - sk) * 500 / (1000 - sk); /* SKEW: a warped phase */
        if (sh == 0)
            y = sine_i((uint32_t)q * 4294967u);
        else if (sh == 1)
            y = q < 250 ? q * 131 : q < 750 ? (500 - q) * 131 : (q - 1000) * 131;
        else
            y = q < 500 ? 30000 : -30000;
        y = y * (100 + v[2] * 3) / 100;                                 /* FOLD: overdrive, then fold back */
        while (y > 32767 || y < -32767)
            y = y > 0 ? 65534 - y : -65534 - y;
        y = VMID - y * (VH / 2 - 2) / 32767;
        vz_vline(VX0 + i, y, VMID, vz_fill(c));
        if (i)
            cv_line_t(VX0 + i - 1, py, VX0 + i, y, c, 2);
        py = y;
    }
}

static void viz_random(const int16_t *v, uint16_t c, uint32_t f)
{
    /* 16 values as stepped bars from the middle; SMOOTH draws the glide between them; SPREAD scales them; BIAS
     * shifts them */
    int32_t k, sw = VW / 16, val[16];
    (void)f;
    vz_head(ICON_W_SH, "RANDOM", c);
    vz_dots(VMID, T_MID);
    vz_seed = 4242u;
    for (k = 0; k < 16; k++)
        val[k] = clamp(((int32_t)(vz_rand() % 2001u) - 1000) * v[2] / 100 + v[3] * 10, -1000, 1000);
    for (k = 0; k < 16; k++) {
        int32_t x = VX0 + k * sw, y = VMID - val[k] * (VH / 2 - 2) / 1000;
        cv_rect(x + 1, y < VMID ? y : VMID, sw - 3, (y < VMID ? VMID - y : y - VMID) + 1, vz_fill(c));
        cv_rect(x + 1, y - 1, sw - 3, 3, c);                            /* the step's value: a bold cap */
        if (v[1] && k) {                                                /* SMOOTH: the glide from the last value */
            int32_t py = VMID - val[k - 1] * (VH / 2 - 2) / 1000, gl = v[1] * (sw - 3) / 100;
            cv_line_t(x - 2, py, x + 1 + gl, y, c, 2);
        }
    }
}

static void viz_adsr(const int16_t *v, uint16_t c, uint32_t f)
{
    /* the times (0..127 on TIME_MS_X10: 1 ms .. 10 s) on a square-root scale so short and long both read */
    int32_t a = (int32_t)TIME_MS_X10[v[0] & 127], d = (int32_t)TIME_MS_X10[v[1] & 127], r = (int32_t)TIME_MS_X10[v[3] & 127];
    int32_t sa = 1, sd = 1, sr = 1, tot, xs[5], ys[5], x, k, base = VY1 - 18, top = VY0;
    int32_t sus = base - v[2] * (base - top) / 100;
    static const char *const L[4] = {"A", "D", "S", "R"};
    while (sa * sa < a) sa++;
    while (sd * sd < d) sd++;
    while (sr * sr < r) sr++;
    tot = sa + sd + sr + (sa + sd + sr) / 3;                            /* (a sustain plateau a third as long) */
    xs[0] = VX0;
    xs[1] = VX0 + sa * VW / tot;
    xs[2] = xs[1] + sd * VW / tot;
    xs[3] = xs[2] + (sa + sd + sr) / 3 * VW / tot;
    xs[4] = VX1;
    ys[0] = base; ys[1] = top; ys[2] = sus; ys[3] = sus; ys[4] = base;
    vz_head(ICON_ENV, "ENVELOPE", c);
    vz_dots(base, T_MID);
    for (k = 0; k < 4; k++) {                                           /* the body, filled, then the edge */
        for (x = xs[k]; x <= xs[k + 1]; x++) {
            int32_t span = xs[k + 1] - xs[k], y = span ? ys[k] + (ys[k + 1] - ys[k]) * (x - xs[k]) / span : ys[k];
            vz_vline(x, y, base, vz_fill(c));
        }
    }
    for (k = 0; k < 4; k++)
        cv_line_t(xs[k], ys[k], xs[k + 1], ys[k + 1], c, 2);
    for (k = 0; k < 4; k++) {                                           /* the stage tags, the last-turned solid */
        int32_t cx = (xs[k] + xs[k + 1]) / 2 - 6, on = f == (uint32_t)k;
        if (on)
            vz_tag(cx, VY1 - 14, L[k], c, T_BG);
        else
            cv_text_in(cx, VY1 - 13, 13, &AF_S, L[k], T_MID, T_BG);
    }
}

static void viz_seq(const int16_t *v, uint16_t c, uint32_t f)
{
    /* the 16 steps as blocks in four groups of four (their values arrive with p-locks, phase 7); STEPS lit, SWING
     * nudges the offbeats right, SLEW joins each step to the next */
    int32_t k, bw = 11, gap = 2, ggap = 6, y = VMID - 8;
    (void)f;
    vz_head(ICON_STEPS, "SEQUENCE", c);
    for (k = 0; k < 16; k++) {
        int32_t x = VX0 + 2 + k * (bw + gap) + (k / 4) * ggap + ((k & 1) ? v[3] * 4 / 100 : 0), on = k < v[0];
        cv_rrect(x, y, bw, 16, 2, on ? c : T_RAISE, T_BG);
        if (on && v[2] && k + 1 < v[0])                                 /* SLEW: a bar into the next step */
            cv_rect(x + bw, y + 7, gap + ((k & 3) == 3 ? ggap : 0) + 1, 2 + v[2] / 50, c);
        if ((k & 3) == 0) {                                             /* the group's first step, numbered */
            char n[4];
            fmt_int(n, k + 1);
            cv_text_on(x, y + 20, &AF_S, n, T_MID, T_BG);
        }
    }
}

/* ------------------------------------------------------------- MIXER --- */
static int32_t meter_w(int32_t peak, int32_t w)        /* 0..w on a log scale (-48 dB .. 0 dBFS) */
{
    int32_t lg = 0, v;
    if (peak < 128)
        return 0;
    while ((peak >> lg) > 1)
        lg++;
    v = lg * 8 + (((peak << 3) >> lg) & 7);
    return clamp((v - 56) * w / 64, 0, w);
}

static void viz_mixer(void)
{
    uint32_t t;
    cv_icon_mid(VX0, 10, 16, ICON_X_MIXER, T_THEME, T_BG);
    cv_text_on(VX0 + 21, 3, &AF_S, ui.glo_held ? "HOLD GLO + KEY 1-4: TRACK" : "MIXER", T_MID, T_BG);
    for (t = 0; t < NTRK; t++) {
        int32_t x = 8 + 58 * (int32_t)t, sel = t == sys.sel, lv = track[t].level * 50 / 127;
        int32_t m = meter_w(track_rt[t].peak, 50), top = VY0 - 4, h = VY1 - top;
        char n[3] = {'T', (char)('1' + t), 0};
        uint16_t bg = sel ? T_SURF : T_BG;
        cv_rrect(x, top, 52, h + 4, 5, bg, T_BG);
        if (sel)
            cv_rect(x + 8, top + h + 1, 36, 2, T_ACCENT);       /* the focused track: a bar at the foot, not colour alone */
        cv_text_on(x + 6, top + 4, &AF_M, n, sel ? T_TEXT : T_MID, bg);
        cv_rrect(x + 40, VY1 - 44, 5, 44, 2, T_RAISE, bg);     /* the level, right of the words */
        cv_rrect(x + 40, VY1 - lv * 44 / 50, 5, lv * 44 / 50 < 3 ? 3 : lv * 44 / 50, 2, T_THEME, T_RAISE);
        cv_rrect(x + 47, VY1 - 44, 3, 44, 1, T_RAISE, bg);     /* the meter */
        if (m)
            cv_rrect(x + 47, VY1 - m * 44 / 50, 3, m * 44 / 50 < 3 ? 3 : m * 44 / 50, 1, track[t].mute ? T_DIM : T_TEXT, T_RAISE);
        if (track[t].mute) {                                    /* muted: the crossed speaker on a solid key */
            cv_rrect(x + 6, top + 26, 24, 20, 4, T_ACCENT, bg);
            cv_icon_mid(x + 10, top + 36, 16, ICON_MUTE, T_BG, T_ACCENT);
        }
    }
}

/* the panel: its signature (what it shows), then the picture */
static uint32_t viz_sig(void)
{
    uint32_t h = 2166136261u + ui.view * 7u + ui.kind * 31u + ui.dev * 131u + ui.slot * 1009u + ui.glo_held * 3u +
                 (ui.last < 4u ? ui.last + 1u : 0u) * 7919u + sys.sel * 104729u, k, t;
    if (ui.view == VIEW_MIXER) {
        for (t = 0; t < NTRK; t++)
            h = (h ^ ((uint32_t)meter_w(track_rt[t].peak, 50) | (uint32_t)track[t].mute << 8 | (uint32_t)track[t].level << 9)) *
                16777619u;
        return h;
    }
    for (k = 0; k < 4u; k++) {
        int16_t *vp;
        ui_page(k, &vp);
        h = (h ^ (uint32_t)(*vp + 32768)) * 16777619u;
    }
    if (ui.kind == FOCUS_DEV && ui.dev == DEV_GRAIN)                   /* GRAIN draws TAPE's loop window too */
        h = (h ^ (uint32_t)(tp[sys.sel].dev[DEV_SRC][0] << 8 | tp[sys.sel].dev[DEV_SRC][1])) * 16777619u;
    return h + (ui.kind == FOCUS_SLOT ? tp[sys.sel].engine[ui.slot] * 65537u : 0u);
}

static void draw_viz(void)
{
    uint32_t sig = viz_sig(), f = ui.last;
    int16_t v[4], *vp;
    uint32_t k;
    if (!ui.force && sig == ui.sig_viz)
        return;
    ui.sig_viz = sig;
    cv_begin(240, VZ_H, T_BG);
    if (ui.view == VIEW_MIXER) {
        viz_mixer();
    } else {
        for (k = 0; k < 4u; k++) {
            ui_page(k, &vp);
            v[k] = *vp;
        }
        if (ui.kind == FOCUS_SLOT) {
            uint16_t c = SLOT_COLOR[ui.slot];
            switch (tp[sys.sel].engine[ui.slot]) {
            case ME_WAVE: viz_wave(v, c, f); break;
            case ME_RANDOM: viz_random(v, c, f); break;
            case ME_ADSR: viz_adsr(v, c, f); break;
            default: viz_seq(v, c, f); break;
            }
        } else {
            switch (ui.dev) {
            case DEV_SRC: viz_tape(v, f); break;
            case DEV_GRAIN: viz_grain(v, f); break;
            case DEV_RESO: viz_reso(v, f); break;
            case DEV_COLOR: viz_color(v, f); break;
            default: viz_space(v, f); break;
            }
        }
    }
    cv_blit(0, UI_VIZ_Y);
}

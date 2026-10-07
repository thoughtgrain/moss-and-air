/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: the visualization panel under the pictograms (y 124..215, dot rows 0..45 of its own canvas). One picture
 * per view, drawn from the parameter values (not from audio), so it is right the moment a knob turns and costs
 * nothing while nothing changes.
 *
 * The look follows the pictograms above it (ui_px.c): one ink on the background, 1-dot strokes, square nodes where
 * a value sits, dotted drop lines and guides, small 3 x 5 labels under the plot, the way a groovebox draws an
 * envelope. The page's last-turned knob gets its label inverted here too, where the picture has one.
 *
 *   TAPE       a reel-to-reel: the run between the guides is the whole tape, the loop window bracketed on it
 *              (STRT, LEN), the playhead under it, SPD as chevrons between the reels, DUB as layers over the window
 *   GRAIN      the sample in TAPE's loop window, lit where grains read it (SIZE, scaled by PITCH), and one solid
 *              block per grain (DENS) in a stereo lane under it (SPRD, up = left, down = right)
 *   RESONATOR  the response over 8 octaves: peaks on ROOT's harmonics (a node on each, the root's filled and
 *              named), as sharp as FDBK makes them, rolling off with DAMP, blended with the dotted dry line by MIX
 *   COLOR      a sine through the device (dotted: in; solid: out); the inset follows the last-turned knob: the
 *              transfer curve (DRIVE, CRUSH), the noise (NOISE) or the tone filter (TONE)
 *   SPACE      the dry hit, the echoes as stems with nodes (TIME apart, falling by FDBK) and the reverb tail
 *   MOD slots  WAVE's shape, RANDOM's steps, ADSR's envelope with its stages named, SEQ's 16 steps
 *   MIXER      the four tracks' meters, mutes, and which one is focused
 *
 * The DSP of each device will use the same mappings as these pictures (the comments name them), so what is drawn
 * is what is heard. Integer only: no float on the device. */

#define VZ_H 92                      /* px */
#define DX0 2                        /* the plot, in dots */
#define DX1 117
#define DW (DX1 - DX0)
#define DY0 1
#define DY1 34                       /* the plot's floor */
#define DH (DY1 - DY0)
#define DMID ((DY0 + DY1) / 2)
#define DLBL 38                      /* the label row (3 x 5) */

static uint32_t vz_seed;
static uint32_t vz_rand(void)        /* deterministic: the same picture for the same values */
{
    vz_seed = vz_seed * 1664525u + 1013904223u;
    return vz_seed >> 8;
}

/* a value's node: a 3 x 3 square, solid or open, centred on (x, y) */
static void vz_node(int32_t x, int32_t y, int solid)
{
    if (solid)
        px_box(x - 1, y - 1, 3, 3, px_ink);
    else
        px_frame(x - 1, y - 1, 3, 3, px_ink, 1);
}

/* a label under the plot centred on x (kept on the panel); inverted when it names the last-turned knob */
static void vz_label(int32_t x, const char *s, int on)
{
    int32_t w = px_text_w(PXF_3, s), x0 = clamp(x - w / 2, 1, 118 - w);
    if (on)
        px_tag(x0 - 1, DLBL - 1, PXF_3, s, px_ink, px_bg);
    else
        px_text(x0, DLBL, PXF_3, s, px_ink);
}

/* a polyline through n points of xs, ys */
static void vz_poly(const int32_t *xs, const int32_t *ys, int32_t n, uint16_t c, int32_t step)
{
    int32_t k;
    for (k = 1; k < n; k++)
        px_line(xs[k - 1], ys[k - 1], xs[k], ys[k], c, step);
}

/* -------------------------------------------------------------- TAPE --- */
/* a reel: its flange (dotted), the tape wound on it (solid), the hub and three spokes */
static void vz_reel(int32_t cx, int32_t cy)
{
    int32_t k;
    px_ring(cx, cy, 11, px_dim, 2);
    px_ring(cx, cy, 8, px_ink, 1);
    px_ring(cx, cy, 7, px_ink, 1);
    px_box(cx - 1, cy - 1, 3, 3, px_ink);
    for (k = 0; k < 3; k++)
        px_line(px_px(cx, 2, 8 + k * 21), px_py(cy, 2, 8 + k * 21), px_px(cx, 5, 8 + k * 21), px_py(cy, 5, 8 + k * 21), px_ink, 1);
}

static void viz_tape(const int16_t *v, uint32_t f)
{
    /* A reel-to-reel: the tape leaves the left reel, runs over two guides along the bottom (the whole tape, start
     * to end, left to right) and winds onto the right reel. The loop window is bracketed on that run (START, LEN),
     * the playhead sits under where playing starts (the end when reversed), SPEED is the chevrons between the
     * reels, and DUB the layers stacked over the window. */
    int32_t ga = 24, gb = 95, run = 31, x0 = ga + v[0] * (gb - ga) / 100, x1 = x0 + v[1] * (gb - ga) / 100, k;
    int32_t lit = (v[3] + 33) / 34;
    if (x1 > gb)
        x1 = gb;
    vz_reel(13, 12);
    vz_reel(106, 12);
    px_line(5, 18, ga - 1, run - 1, px_ink, 1);                        /* off the left reel, onto the right */
    px_line(gb + 1, run - 1, 114, 18, px_ink, 1);
    vz_node(ga, run, 0);                                               /* the guides */
    vz_node(gb, run, 0);
    px_line(ga + 2, run - 1, gb - 2, run - 1, px_ink, 1);              /* the run: a band two dots deep */
    px_line(ga + 2, run + 1, gb - 2, run + 1, px_ink, 1);
    px_line(ga + 2, run, gb - 2, run, px_dim, 2);                      /* empty (phase 2: the take shows here) */
    for (k = 0; k < 2; k++) {                                          /* the brackets, 2 dots wide when turned */
        int32_t x = k ? x1 : x0, s = k ? -1 : 1, w = f == (uint32_t)k ? 2 : 1;
        px_box(k ? x - w + 1 : x, run - 4, w, 9, px_ink);
        px_line(x, run - 4, x + 2 * s, run - 4, px_ink, 1);
        px_line(x, run + 4, x + 2 * s, run + 4, px_ink, 1);
    }
    {   /* the playhead: a triangle under the run, pointing up at where playing starts */
        int32_t px = v[2] < 0 ? x1 - 2 : x0 + 2;
        for (k = 0; k < 3; k++)
            px_box(px - k, run + 3 + k, 1 + 2 * k, 1, px_ink);
    }
    for (k = 0; k < 3; k++) {                                          /* DUB: the layers kept, over the window */
        int32_t lx = x0 + 2 + k * 2, rx = x1 - 2 - k * 2;
        if (rx > lx)
            px_line(lx, run - 6 - k * 2, rx, run - 6 - k * 2, k < lit ? px_ink : px_dim, k < lit ? 1 : 2);
    }
    px_chevrons(60, 11, v[2], px_ink);                                 /* SPEED, between the reels */
    vz_label(x0, "IN", f == 0u);
    if (x1 - x0 >= 22 || f == 1u)
        vz_label(x1, "OUT", f == 1u);
    if (f == 3u || f == 2u)
        vz_label(60, f == 3u ? "DUB" : "SPD", 1);
}

/* ------------------------------------------------------------- GRAIN --- */
/* The sample under the grains: its envelope at position pos (0..1000 of the tape), 0..1000. Until TAPE records
 * (phase 2) it is a demo: a loop of eight decaying hits with some grit, and the panel says DEMO; phase 2 reads the
 * track's tape here instead, and the drawing stays as it is. */
#define TAPE_MS 3300                 /* a tape's length (docs/bryo-architecture.md: 36 KiB of ADPCM at 22.05 kHz) */
static int32_t tape_env(uint32_t t, int32_t pos)
{
    int32_t d = pos % 125, e = d < 50 ? 1000 - d * 18 : 100 - (d - 50);   /* a hit every 1/8, ~50 ms decay */
    uint32_t h = (uint32_t)pos * 2654435761u + t * 97u;
    return clamp(e + (int32_t)(h >> 26) * 4 - 120, 30, 1000);
}

static void viz_grain(const int16_t *v, uint32_t f)
{
    /* The view spans TAPE's loop window (START, LEN). DENS 0..100: 1..25 grains, placed along the loop. Each grain
     * reads SIZE ms of the sample, times 2^(PITCH/12) (pitched up, it reads more): that slice of the waveform is
     * lit, and it is the width of its block. SPRD scatters the blocks across the stereo lane under the waveform
     * (up = left). Solid blocks: the grains as the engine schedules them, without the window shape. */
    const int16_t *tv = tp[sys.sel].dev[DEV_SRC];
    int32_t n = 1 + v[1] * 24 / 100, i, x, g, gw = DW - 6;            /* (L and R at the right) */
    int32_t start = tv[0] * 10, len = tv[1] * 10, loop_ms = len * TAPE_MS / 1000, oct = 1000, span;
    int32_t gx[25], gy[25], mid = 11, amp = 10, lane = 28;
    char b[16];
    for (i = 0; i < v[2]; i++) oct = oct * 1059 / 1000;               /* 2^(st/12), 1/1000 */
    for (i = 0; i > v[2]; i--) oct = oct * 1000 / 1059;
    span = clamp(v[0] * oct / 1000 * gw / (loop_ms > 0 ? loop_ms : 1), 1, gw / 2);
    vz_seed = 12345u;
    for (g = 0; g < n; g++) {
        gx[g] = DX0 + (int32_t)(vz_rand() % (uint32_t)(gw - span));
        gy[g] = ((int32_t)(vz_rand() % 2001u) - 1000) * v[3] / 100;    /* -1000 (L) .. 1000 (R) */
    }
    for (x = DX0; x < DX0 + gw; x++) {                                 /* the sample: dim, lit where a grain reads */
        int32_t a = tape_env(sys.sel, start + (x - DX0) * len / gw) * amp / 1000, lit = 0;
        for (g = 0; g < n && !lit; g++)
            lit = x >= gx[g] && x < gx[g] + span;
        px_box(x, mid - a, 1, 2 * a + 1, lit ? px_ink : px_dim);
    }
    px_line(DX0, lane, DX0 + gw, lane, px_dim, 2);                     /* the stereo lane */
    px_text(DX1 - 2, lane - 7, PXF_3, "L", px_ink);
    px_text(DX1 - 2, lane + 3, PXF_3, "R", px_ink);
    for (g = 0; g < n; g++)
        px_box(gx[g], lane - 1 + gy[g] * 5 / 1000, span, 2, px_ink);
    fmt_int(b, n);
    str_cpy(b + str_len(b), " GRAINS", 8);
    if (f == 1u)
        px_tag(1, DLBL - 1, PXF_3, b, px_ink, px_bg);
    else
        px_text(2, DLBL, PXF_3, b, px_ink);
    px_text(118 - px_text_w(PXF_3, "DEMO WAVE"), DLBL, PXF_3, "DEMO WAVE", px_dim);   /* (phase 2: the tape) */
}

/* --------------------------------------------------------- RESONATOR --- */
/* 12 log2(k) in 1/16 semitones: harmonic k's distance above the root */
static const int16_t HARM_16[24] = {0, 192, 304, 384, 446, 496, 539, 576, 609, 638, 664, 688,
                                    710, 731, 750, 768, 785, 801, 816, 830, 843, 856, 869, 880};

/* x: MIDI 28 (E1, 41 Hz) .. 124 (8 octaves) in 1/16 semitones; peaks at ROOT's harmonics; the peak width narrows
 * as FDBK rises; DAMP rolls the upper harmonics off; MIX blends with the flat dry line (35 %); 0..1000 */
static int32_t reso_at(const int16_t *v, int32_t x)
{
    int32_t n16 = 28 * 16 + (x - DX0) * 96 * 16 / DW, width16 = 4 + (100 - v[1]) * 28 / 100, best = 0, k;
    for (k = 0; k < 24; k++) {
        int32_t d = n16 - (v[0] * 16 + HARM_16[k]), amp = 1000 - k * v[2] * 9, pk;
        if (amp <= 0)
            break;
        if (d < -width16 * 6 || d > width16 * 6)
            continue;
        d = d < 0 ? -d : d;
        pk = amp * width16 * width16 / (width16 * width16 + d * d);   /* 1 / (1 + (d / w)^2) */
        if (pk > best)
            best = pk;
    }
    return (best * v[3] + 350 * (100 - v[3])) / 100;
}

static void viz_reso(const int16_t *v, uint32_t f)
{
    int32_t x, k, py = 0, top = DY0 + 6, base = DY1 + 2;
    px_line(DX0, base - 350 * (base - top) / 1000, DX1, base - 350 * (base - top) / 1000, px_dim, 2);   /* dry */
    px_line(DX0, base + 1, DX1, base + 1, px_dim, 2);
    for (x = DX0; x <= DX1; x++) {
        int32_t y = base - reso_at(v, x) * (base - top) / 1000;
        if (x > DX0)
            px_line(x - 1, py, x, y, px_ink, 1);
        py = y;
    }
    for (k = 0; k < 24; k++) {                                         /* a node on each harmonic still sounding */
        int32_t hx = DX0 + (v[0] * 16 + HARM_16[k] - 28 * 16) * DW / (96 * 16);
        if (hx > DX1 - 1 || 1000 - k * v[2] * 9 <= 0 || (k && hx - (DX0 + (v[0] * 16 + HARM_16[k - 1] - 28 * 16) * DW / (96 * 16)) < 3))
            break;                                                     /* (closer than 3 dots: the rest are a blur) */
        vz_node(hx, base - reso_at(v, hx) * (base - top) / 1000, k == 0);
    }
    {   /* ROOT: its note over its node */
        int32_t rx = DX0 + (v[0] * 16 - 28 * 16) * DW / (96 * 16);
        char b[8];
        const char *u;
        param_format(&DEV_P[DEV_RESO][0], v[0], b, &u);
        if (f == 0u)
            px_tag(clamp(rx - 3, 1, 110), 0, PXF_3, b, px_ink, px_bg);
        else
            px_text(clamp(rx - 2, 1, 110), 0, PXF_3, b, px_ink);
    }
    vz_label(DX0 + 8, "E1", 0);
    vz_label(DX1 - 8, "E9", 0);
    vz_label((DX0 + DX1) / 2, f == 1u ? "FDBK" : f == 2u ? "DAMP" : f == 3u ? "MIX" : "HARMONICS", f >= 1u && f <= 3u);
}

/* ------------------------------------------------------------- COLOR --- */
/* DRIVE 0..100 -> gain 1..16 into the soft clip; CRUSH 0..100 -> 16..2 bits and a hold of 1..12 samples (the
 * rate); NOISE: noise riding the signal's envelope; TONE: a one-pole low-pass on that noise */
static int32_t color_shape(int32_t x, const int16_t *v)             /* Q15 in -> Q15 out */
{
    int32_t g = 1 + v[0] * 15 / 100, y = softclip(x * g) * 32767 / softclip(32767 * g);
    int32_t bits = 16 - v[1] * 14 / 100, q = 1 << (16 - bits);
    return q > 1 ? (y / q) * q : y;
}

static void viz_color(const int16_t *v, uint32_t f)
{
    static const char *const NAME[4] = {"DRIV", "CRSH", "NOIS", "TONE"};
    int32_t bx = DX0, bw = 32, wx = DX0 + 37, ww = DX1 - wx, i, py = 0, pi = 0, held = 0, hold = 1 + v[1] * 11 / 100;
    int32_t lp = 0, a = 3 * (100 - v[3]) / 4 + 8, cy = DMID + 1, amp = DH / 2;   /* TONE: the low-pass step */
    px_frame(bx, DY0, bw, DH + 2, px_dim, 2);                          /* the inset */
    if (f == 3u) {                                                     /* the tone filter's response */
        int32_t pyc = 0;
        for (i = 0; i < bw - 4; i++) {
            int32_t fr = i * 100 / (bw - 4), gg = 100 * a / (a + fr + 1), y = DY1 - 2 - gg * (DH - 6) / 100;
            if (i)
                px_line(bx + 1 + i, pyc, bx + 2 + i, y, px_ink, 1);
            pyc = y;
        }
    } else if (f == 2u) {                                              /* the noise: dots, as many as NOISE */
        vz_seed = 99u;
        for (i = 0; i < 4 + v[2] * 2; i++)
            px_dot(bx + 2 + (int32_t)(vz_rand() % (uint32_t)(bw - 4)), DY0 + 2 + (int32_t)(vz_rand() % (uint32_t)(DH - 2)), px_ink);
    } else {                                                           /* the transfer curve */
        int32_t pyc = 0;
        px_line(bx + 2, cy, bx + bw - 3, cy, px_dim, 2);
        for (i = 0; i < bw - 4; i++) {
            int32_t x = (i * 2 - (bw - 5)) * 32767 / (bw - 5), y = cy - color_shape(x, v) * (amp - 3) / 32767;
            if (i)
                px_line(bx + 1 + i, pyc, bx + 2 + i, y, px_ink, 1);
            pyc = y;
        }
    }
    px_line(wx, cy, DX1, cy, px_dim, 2);
    vz_seed = 777u;
    for (i = 0; i <= ww; i++) {                                        /* two cycles of a sine: in dotted, out solid */
        int32_t s = sine_i((uint32_t)i * (0xFFFFFFFFu / (uint32_t)ww) * 2u), y, yi, nz, env;
        if (i % hold == 0)
            held = color_shape(s, v);                                  /* CRUSH's rate: hold a value */
        env = s < 0 ? -s : s;
        nz = ((int32_t)(vz_rand() & 0xFFFF) - 32768) * v[2] / 100;      /* NOISE, before TONE */
        lp += (nz - lp) * a / 100;
        y = cy - clamp(held + (lp * env >> 16), -32767, 32767) * (amp - 1) / 32767;
        yi = cy - s * (amp - 1) / 32767;
        if (i) {
            if (i % 2 == 0)
                px_dot(wx + i, yi, px_dim);
            px_line(wx + i - 1, py, wx + i, y, px_ink, 1);
        }
        py = y;
        pi = yi;
    }
    (void)pi;
    vz_label(bx + bw / 2, f < 4u ? NAME[f] : "CURVE", f < 4u);
    vz_label(wx + ww / 2, "IN : OUT", 0);
}

/* ------------------------------------------------------------- SPACE --- */
/* the window is 2 s: the dry hit at 0, echoes TIME ms apart falling by FDBK, the tail rising over SIZE (10..90 ms)
 * and falling over DECAY (0.2..4.2 s) */
static void viz_space(const int16_t *v, uint32_t f)
{
    int32_t t, x, py = DY1, rise = 10 + v[2] * 80 / 100, len = 200 + v[3] * 40, g = 32767, first = -1;
    px_line(DX0, DY1 + 1, DX1, DY1 + 1, px_dim, 2);
    for (x = DX0; x <= DX1; x++) {                                     /* the tail: a dim hatch under its edge */
        int32_t ms = (x - DX0) * 2000 / DW, e = ms < rise ? ms * 1000 / rise : 1000 - (ms - rise) * 1000 / len, y;
        if (e <= 0)
            break;
        e = e * e / 1000 * 55 / 100;
        y = DY1 - e * DH / 1000;
        if ((x & 1) == 0 && y < DY1)
            px_line(x, y + 2, x, DY1, px_dim, 2);
        if (x > DX0)
            px_line(x - 1, py, x, y, px_ink, 1);
        py = y;
    }
    px_line(DX0 + 1, DY1, DX0 + 1, DY0 + 1, px_ink, 1);                /* the dry hit */
    vz_node(DX0 + 1, DY0 + 1, 1);
    for (t = v[0]; t < 2000 && g > 1500; t += v[0]) {                  /* the echoes: stems with nodes */
        int32_t h;
        g = (t == v[0]) ? 26000 : g * v[1] / 100;
        x = DX0 + t * DW / 2000;
        h = g * DH / 32767;
        if (first < 0)
            first = x;
        px_line(x, DY1, x, DY1 - h, px_ink, 1);
        vz_node(x, DY1 - h, f <= 1u);
    }
    px_text(DX0, DLBL, PXF_3, "DRY", px_ink);
    if (first >= DX0 + 20 && first < DX1 - 20)
        vz_label(first + 2, f == 1u ? "FDBK" : "ECHO", f <= 1u);
    vz_label(DX1 - 10, f == 2u ? "SIZE" : f == 3u ? "DEC" : "TAIL", f >= 2u);
}

/* ------------------------------------------------------------- MODS --- */
static void viz_wave(const int16_t *v, uint32_t f)
{
    /* SHAPE 0 sine, 1 triangle, 2 square; SKEW moves the peak (-100..100); FOLD folds the top back (0..100) */
    static const char *const NAME[3] = {"SINE", "TRIANGLE", "SQUARE"};
    int32_t i, py = DMID, sk = clamp(500 + v[3] * 5, 50, 950), sh = clamp(v[1], 0, 2);
    px_line(DX0, DMID, DX1, DMID, px_dim, 2);
    for (i = 0; i <= DW; i++) {
        int32_t p = (i * 2000 / DW) % 1000, q, y;                       /* two cycles */
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
        y = DMID - y * (DH / 2 - 1) / 32767;
        if (i)
            px_line(DX0 + i - 1, py, DX0 + i, y, px_ink, 1);
        py = y;
    }
    vz_label(DX0 + 14, NAME[sh], f == 1u);
    if (f == 2u || f == 3u)
        vz_label(DX1 - 10, f == 2u ? "FOLD" : "SKEW", 1);
}

static void viz_random(const int16_t *v, uint32_t f)
{
    /* 16 values as steps around the middle; SMTH draws the glide between them; SPRD scales them; BIAS shifts them */
    int32_t k, sw = 7, val[16], py = DMID;
    px_line(DX0, DMID, DX1, DMID, px_dim, 2);
    vz_seed = 4242u;
    for (k = 0; k < 16; k++)
        val[k] = clamp(((int32_t)(vz_rand() % 2001u) - 1000) * v[2] / 100 + v[3] * 10, -1000, 1000);
    for (k = 0; k < 16; k++) {
        int32_t x = DX0 + 2 + k * sw, y = DMID - val[k] * (DH / 2 - 1) / 1000, gl = v[1] * (sw - 1) / 100;
        if (k)
            px_line(x - 1, py, x + gl, y, px_ink, 1);                   /* the move into this value (SMTH leans it) */
        px_line(x + gl, y, x + sw - 1, y, px_ink, 1);
        vz_node(x + gl, y, 1);
        py = y;
    }
    vz_label(DX0 + 10, f == 1u ? "SMTH" : f == 2u ? "SPRD" : f == 3u ? "BIAS" : "STEPS", f >= 1u);
}

static void viz_adsr(const int16_t *v, uint32_t f)
{
    /* the times (0..127 on TIME_MS_X10: 1 ms .. 10 s) on a square-root scale so short and long both read */
    int32_t a = (int32_t)TIME_MS_X10[v[0] & 127], d = (int32_t)TIME_MS_X10[v[1] & 127], r = (int32_t)TIME_MS_X10[v[3] & 127];
    int32_t sa = 1, sd = 1, sr = 1, tot, xs[5], ys[5], k, base = DY1, top = DY0 + 2, w = DW - 4;
    static const char *const L[4] = {"ATK", "DEC", "SUS", "REL"};
    while (sa * sa < a) sa++;
    while (sd * sd < d) sd++;
    while (sr * sr < r) sr++;
    tot = sa + sd + sr + (sa + sd + sr) / 3;                            /* (a sustain plateau a third as long) */
    xs[0] = DX0 + 2;
    xs[1] = xs[0] + sa * w / tot;
    xs[2] = xs[1] + sd * w / tot;
    xs[3] = xs[2] + (sa + sd + sr) / 3 * w / tot;
    xs[4] = DX1 - 2;
    ys[0] = base;
    ys[1] = top;
    ys[2] = ys[3] = base - v[2] * (base - top) / 100;
    ys[4] = base;
    px_line(DX0, base + 1, DX1, base + 1, px_dim, 2);
    vz_poly(xs, ys, 5, px_ink, 1);
    for (k = 1; k < 4; k++) {                                          /* nodes, and drop lines to the floor */
        px_line(xs[k], ys[k] + 2, xs[k], base, px_dim, 2);
        vz_node(xs[k], ys[k], 0);
    }
    vz_node(xs[0], ys[0], 1);
    vz_node(xs[4], ys[4], 1);
    for (k = 0; k < 4; k++)                                            /* the stages, named under their stretch */
        vz_label((xs[k] + xs[k + 1]) / 2 + 1, L[k], f == (uint32_t)k);
}

static void viz_seq(const int16_t *v, uint32_t f)
{
    /* the 16 steps in four groups of four (their values arrive with p-locks, phase 7); STEPS lit, SWING nudges the
     * off-beats late, SLEW joins each step to the next */
    int32_t k, bw = 5, gap = 1, gg = 3, y = 6, h = 16, sw = v[3] * 2 / 100;
    for (k = 0; k < 16; k++) {
        int32_t x = DX0 + 3 + k * (bw + gap) + (k / 4) * gg + ((k & 1) ? sw : 0), on = k < v[0];
        if (on)
            px_box(x, y, bw, h, px_ink);
        else
            px_frame(x, y, bw, h, px_dim, 2);
        if (on && v[2] && k + 1 < v[0])                                /* SLEW: a ramp into the next step */
            px_line(x + bw, y + h - 1 - v[2] * (h - 2) / 100, x + bw + gap + ((k & 3) == 3 ? gg : 0), y, px_ink, 1);
        if ((k & 3) == 0) {
            char n[4];
            fmt_int(n, k + 1);
            px_text(x, y + h + 3, PXF_3, n, px_ink);
        }
    }
    vz_label(60, f == 0u ? "LEN" : f == 1u ? "RATE" : f == 2u ? "SLEW" : f == 3u ? "SWNG" : "SEQUENCE", f < 4u);
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

/* a speaker, crossed when muted, with sound waves when not */
static const char *const PX_SPK[7] = {"..#....", ".##....", "###....", "###....", "###....", ".##....", "..#...."};

static void viz_mixer(void)
{
    uint32_t t;
    for (t = 0; t < NTRK; t++) {
        int32_t x = 30 * (int32_t)t, m = meter_w(track_rt[t].peak, 10), k;
        char n[3] = {'T', (char)('1' + t), 0};
        for (k = 0; k < 10; k++) {                                     /* the meter: 10 segments from the floor */
            int32_t yy = DY1 - 2 - k * 3;
            if (k < m)
                px_box(x + 5, yy, 6, 2, track[t].mute ? px_dim : px_ink);
            else
                px_line(x + 5, yy, x + 10, yy, px_dim, 2);
        }
        px_art(x + 15, DY1 - 9, PX_SPK, 7, track[t].mute ? px_dim : px_ink);
        if (track[t].mute) {
            px_line(x + 19, DY1 - 8, x + 23, DY1 - 4, px_ink, 1);
            px_line(x + 19, DY1 - 4, x + 23, DY1 - 8, px_ink, 1);
        } else {
            px_line(x + 19, DY1 - 7, x + 19, DY1 - 5, px_ink, 1);
            px_line(x + 21, DY1 - 9, x + 21, DY1 - 3, px_ink, 1);
        }
        if (t == sys.sel)                                              /* the focused track: inverted, not colour */
            px_tag(x + 4, DLBL - 1, PXF_3, n, px_ink, px_bg);
        else
            px_text(x + 5, DLBL, PXF_3, n, px_ink);
        if (track[t].mute)
            px_text(x + 15, DLBL, PXF_3, "MUTE", px_ink);
    }
}

/* a message: an inverted box over the panel, its words wrapped at 18 characters */
static void viz_message(void)
{
    char line[20];
    const char *s = ui.msg;
    int32_t y = 8, n = 0;
    px_box(2, 4, 116, 32, px_ink);
    while (*s && y < 34) {
        const char *e = s, *brk = 0;
        int32_t len;
        while (*e && e - s <= 18) {
            if (*e == ' ')
                brk = e;
            e++;
        }
        len = *e && brk ? (int32_t)(brk - s) : (int32_t)(e - s);
        str_cpy(line, s, (uint32_t)len + 1u);
        px_text_c(2, 116, y, PXF_5, line, px_bg);
        s += len;
        while (*s == ' ')
            s++;
        y += 10;
        n++;
    }
    (void)n;
}

/* the panel: its signature (what it shows), then the picture */
static uint32_t viz_sig(void)
{
    uint32_t h = 2166136261u + ui.view * 7u + ui.kind * 31u + ui.dev * 131u + ui.slot * 1009u + ui.glo_held * 3u +
                 (ui.last < 4u ? ui.last + 1u : 0u) * 7919u + sys.sel * 104729u + ux.theme * 3u, k, t;
    if (ui.msg_t)
        return hash_str(h ^ 0x5A5Au, ui.msg);
    if (ui.view == VIEW_MIXER) {
        for (t = 0; t < NTRK; t++)
            h = (h ^ ((uint32_t)meter_w(track_rt[t].peak, 10) | (uint32_t)track[t].mute << 8)) * 16777619u;
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
    px_colors();
    cv_begin(240, VZ_H, T_BG);
    if (ui.msg_t) {
        viz_message();
    } else if (ui.view == VIEW_MIXER) {
        viz_mixer();
    } else {
        for (k = 0; k < 4u; k++) {
            ui_page(k, &vp);
            v[k] = *vp;
        }
        if (ui.kind == FOCUS_SLOT) {
            switch (tp[sys.sel].engine[ui.slot]) {
            case ME_WAVE: viz_wave(v, f); break;
            case ME_RANDOM: viz_random(v, f); break;
            case ME_ADSR: viz_adsr(v, f); break;
            default: viz_seq(v, f); break;
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

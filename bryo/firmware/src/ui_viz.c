/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: the visualization panel under the pictograms (y 124..215, dot rows 0..45 of its own canvas). One picture
 * per view, drawn from the parameter values (not from audio), so it is right the moment a knob turns and costs
 * nothing while nothing changes.
 *
 * The look follows the pictograms above it (ui_px.c): one ink on the background, 1-dot strokes, square nodes where
 * a value sits, dotted drop lines and guides, small 3 x 5 labels under the plot, the way a groovebox draws an
 * envelope. The page's last-turned knob gets its label inverted here too, where the picture has one.
 *
 *   SYNTH      per page: the oscillators' cycles, the filter's response, the envelope, the keys and voices
 *   POLY       per page: the sound with STRT and the keys' range, the envelope, the filter with its TYPE
 *   TAPE       a reel-to-reel, reels close over the middle: the run along the bottom is the whole tape with the
 *              sample on it, lit inside the loop window (STRT, LEN, bracketed); the playhead over it (stopped: where
 *              playing starts, moved by ROTA); between the reels SPD as chevrons and DUB as the layers kept (the old
 *              take under, the new on top)
 *   GRAIN      the sound in TAPE's loop window, lit where grains read it (SIZE, scaled by PITCH), and one solid
 *              block per grain (RATE, placed by PATN) in a stereo lane under it (SPRD, up = left, down = right)
 *   RESONATOR  the response over 8 octaves: peaks on PTCH's partials (harmonics, or a scale's chord tones; a node on
 *              each, the root's filled and named), as sharp as DEC makes them, kept up top by TONE, shaped by the
 *              filter before them (dotted: CUT RES SLOP), blended with the dotted dry line by WET
 *   COLOR      a sine through the device (dotted: in; solid: out); the inset follows the last-turned knob: the
 *              transfer curve (DRIV, CRSH, CMOD), the noise after a hit (NOIS, NDEC), its filter (NTON), TILT
 *   SPACE      a sonar from above: the dry hit at the centre, each echo a ring (TIME apart, kept by FDBK) with a
 *              notch (thrown left and right by SPRD), the reverb roughening the rings and dithering the disc
 *   MOD slots  LFO's shape (RND: its loop of steps) and its next time round dotted, ADSR's envelope with its
 *              stages named and bent, SEQ's 16 steps as bars of their values, FOLLOW's envelope over what it listens to
 *   MIXER      the selected track's channel: EQ and filter as one response, the pan as two speakers
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

/* the last-turned knob of a page 2 as an inverted tag, "MIX 80%", its right end at xr (in 3 x 5) */
static void vz_ktag(int32_t xr, int32_t y, const pdesc_t *d, int32_t v)
{
    char b[16], val[12];
    const char *u;
    param_format(d, v, val, &u);
    str_cpy(b, d->label, sizeof b);
    str_cpy(b + str_len(b), " ", 2);
    str_cpy(b + str_len(b), val, 8);
    str_cpy(b + str_len(b), u, 4);
    px_tag(xr - px_text_w(PXF_3, b) - 1, y, PXF_3, b, px_ink, px_bg);
}

/* a gain in dB as a factor, x1000 (2x per 6 dB, straight between) */
static int32_t db_x1000(int32_t db)
{
    int32_t f = 1000;
    for (; db >= 6; db -= 6)
        f *= 2;
    for (; db <= -6; db += 6)
        f /= 2;
    return db >= 0 ? f + f * db / 6 : f - f * (-db) / 12;
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
    px_ring(cx, cy, 10, px_dim, 2);
    px_ring(cx, cy, 7, px_ink, 1);
    px_ring(cx, cy, 6, px_ink, 1);
    px_box(cx - 1, cy - 1, 3, 3, px_ink);
    for (k = 0; k < 3; k++)
        px_line(px_px(cx, 2, 8 + k * 21), px_py(cy, 2, 8 + k * 21), px_px(cx, 4, 8 + k * 21), px_py(cy, 4, 8 + k * 21), px_ink, 1);
}

static void viz_tape(const int16_t *v, uint32_t f)
{
    /* A reel-to-reel, the reels close in over the middle the way a deck's are, and under them the tape's run: the
     * whole of what the track plays (its tape or a reel, named at the top left), its sound drawn on it from the
     * blocks' peaks: lit inside the loop window (STRT, LEN, bracketed), dim outside. The playhead is where the head
     * is while it runs (a dotted line through the run), else where playing starts (the end when reversed); between
     * the reels, SPD as chevrons and DUB as the layers kept; REC armed is tagged at the top right. */
    int32_t r0 = 6, r1 = 114, rw = r1 - r0, top = 20, bot = 35, mid = 27, k, x;
    int32_t x0 = r0 + v[0] * rw / 100, x1 = x0 + v[1] * rw / 100, keep = v[3] >= 0 ? 100 : 100 + v[3];
    int32_t sp = v[2] * (v[5] ? -1 : 1) / (v[6] ? 2 : 1), gain = db_x1000(v[7]);   /* REV, HALF; GAIN */
    int32_t fw = 1 + v[4] * 8 / 100;                   /* XFAD (drawn wider than to scale, so it shows) */
    if (x1 > r1)
        x1 = r1;
    vz_reel(36, 9);
    vz_reel(84, 9);
    px_line(r0, top, r1, top, px_ink, 1);                             /* the run's edges */
    px_line(r0, bot, r1, bot, px_ink, 1);
    for (x = r0 + 1; x < r1; x++) {                                    /* the sample (phase 2: the take) */
        int32_t a = clamp(tape_peak_at(sys.sel, (x - r0) * 1000 / rw) * 6 / 1000 * gain / 1000, 0, 6);   /* (clips) */
        px_box(x, mid - a, 1, 2 * a + 1, x >= x0 && x <= x1 ? px_ink : px_dim);
    }
    for (k = 0; k < 2; k++) {                                          /* the brackets, 2 dots wide when turned */
        int32_t bx = k ? x1 : x0, s = k ? -1 : 1, w = f == (uint32_t)k ? 2 : 1;
        px_box(k ? bx - w + 1 : bx, top - 2, w, bot - top + 5, px_ink);
        px_line(bx, top - 2, bx + 2 * s, top - 2, px_ink, 1);
        px_line(bx, bot + 2, bx + 2 * s, bot + 2, px_ink, 1);
    }
    if (v[4] && x1 - x0 > 2 * fw + 2) {               /* XFAD: the ramps at the loop's ends, dotted */
        px_line(x0 + 1, bot - 1, x0 + fw, top + 1, px_ink, 2);
        px_line(x1 - fw, top + 1, x1 - 1, bot - 1, px_ink, 2);
    }
    {   /* the playhead: a triangle over the run, where the head is (or where playing starts) */
        int32_t xr, hd = tape_head(sys.sel), px;
        xr = x0 + (x1 - x0) * v[9] / 100;              /* ROTATE: where playing starts in the window */
        px = hd >= 0 ? r0 + hd * rw / 1000 : sp < 0 ? (v[9] ? xr - 1 : x1 - 3) : (v[9] ? xr : x0 + 3);
        for (k = 0; k < 3; k++)
            px_box(px - 2 + k, top - 5 + k, 5 - 2 * k, 1, px_ink);   /* (rows 15..17: under the reels' flanges) */
        if (hd >= 0) {                                 /* running: a cut through the sound, and its edges */
            px_box(px - 1, top + 1, 3, bot - top - 1, px_bg);
            px_line(px, top + 1, px, bot - 1, px_ink, 2);
        }
    }
    px_text(1, 0, PXF_3, tape_name(sys.sel), px_ink);                /* what it plays */
    if ((sys.rec >> sys.sel) & 1u)
        px_tag(105, 0, PXF_3, "REC", px_ink, px_bg);
    px_chevrons(60, 5, sp, px_ink);                                    /* SPD (with REV, HALF), between the reels */
    for (k = 0; k < 3; k++) {                                          /* DUB: the old take, the new on top */
        int on = k < 2 ? keep > k * 50 : v[3] < 100;
        px_line(52 + k * 2, 16 - k * 2, 68 - k * 2, 16 - k * 2, on ? px_ink : px_dim, on ? 1 : 2);
    }
    vz_label(x0, "IN", f == 0u);
    if (x1 - x0 >= 22 || f == 1u)
        vz_label(x1, "OUT", f == 1u);
    if (f == 3u || f == 2u)
        vz_label(60, f == 3u ? "DUB" : "SPD", 1);
    if (f >= 4u && f < 12u)
        vz_ktag(78, DLBL - 1, &DEV_P[DEV_SRC][f], v[f]);
}

/* ------------------------------------------------------------- GRAIN --- */
static void viz_grain(const int16_t *v, uint32_t f)
{
    /* The view spans TAPE's loop window (START, LEN). RATE 0..100: 1..25 grains, placed along the loop by PATN (EVEN
     * spaced, SWNG with the off-beats late, CLST in clusters of four, RND scattered). Each grain reads SIZE ms of the
     * sample, times 2^(PTCH/12) (pitched up, it reads more; PRND varies that per grain): that slice of the waveform
     * is lit, and it is the width of its block. SPRD scatters the blocks across the stereo lane under the waveform
     * (up = left). Solid blocks: the grains as the engine schedules them, without the window shape. */
    const int16_t *tv = tp[sys.sel].dev[DEV_SRC];
    int32_t n = 1 + v[1] * 24 / 100, i, x, g, gw = DW - 6;            /* (L and R at the right) */
    int32_t start = tv[0] * 10, len = tv[1] * 10, loop_ms = len * tape_ms(sys.sel) / 1000, oct = 1000, span;
    int32_t gx[25], gy[25], mid = 11, amp = 10, lane = 28, jit = v[5] * 6 / 100, taper;
    char b[16];
    for (i = 0; i < v[2]; i++) oct = oct * 1059 / 1000;               /* 2^(st/12), 1/1000 */
    for (i = 0; i > v[2]; i--) oct = oct * 1000 / 1059;
    span = clamp(v[0] * oct / 1000 * gw / (loop_ms > 0 ? loop_ms : 1), 1, gw / 2);
    vz_seed = 12345u;
    for (g = 0; g < n; g++) {
        int32_t room = gw - span, slot = room / n, at;
        switch (v[9]) {                                               /* PATN */
        case 0: at = g * slot; break;                                  /* EVEN */
        case 1: at = g * slot + (g & 1) * slot / 2; break;              /* SWNG: the off-beats half a step late */
        case 2: at = (g / 4) * slot * 4 + (g % 4) * slot / 3; break;    /* CLST: fours, close together */
        default: at = (int32_t)(vz_rand() % (uint32_t)room); break;    /* RND */
        }
        gx[g] = DX0 + clamp(at, 0, room - 1);
        gy[g] = ((int32_t)(vz_rand() % 2001u) - 1000) * v[3] / 100;    /* -1000 (L) .. 1000 (R) */
    }
    for (x = DX0; x < DX0 + gw; x++) {                                 /* the sample: dim, lit where a grain reads */
        int32_t a = tape_peak_at(sys.sel, start + (x - DX0) * len / gw) * amp / 1000, lit = 0;
        for (g = 0; g < n && !lit; g++)
            lit = x >= gx[g] && x < gx[g] + span;
        px_box(x, mid - a, 1, 2 * a + 1, lit ? px_ink : px_dim);
    }
    px_line(DX0, lane, DX0 + gw, lane, px_dim, 2);                     /* the stereo lane */
    px_text(DX1 - 2, lane - 7, PXF_3, "L", px_ink);
    px_text(DX1 - 2, lane + 3, PXF_3, "R", px_ink);
    /* each grain: 3 rows, its top and bottom rows cut in from the ends as CONT smooths it (square at 0);
     * hollow when it plays backwards (REV: that share of them); SPRY as dotted whiskers, where it may land instead */
    taper = span * v[6] / 250;
    for (g = 0; g < n; g++) {
        int32_t yy = lane - 1 + gy[g] * 5 / 1000, back = (g * 37 + 11) % 100 < v[7], span0 = span;
        if (v[11]) {                                                   /* PRND: each grain's pitch, so its read, varies */
            int32_t st = (int32_t)((g * 53u + 7u) % (uint32_t)(2 * v[11] + 1)) - v[11];
            span = clamp(span + span * st / 24, 1, gw / 2);
        }
        if (back) {
            px_line(gx[g], yy, gx[g] + span - 1, yy, px_ink, 1);
            px_line(gx[g] + taper, yy - 1, gx[g] + span - 1 - taper, yy - 1, px_ink, 1);
            px_line(gx[g] + taper, yy + 1, gx[g] + span - 1 - taper, yy + 1, px_ink, 1);
            px_dot(gx[g], yy, px_bg);                                  /* (an arrow's notch: it runs right to left) */
        } else {
            px_box(gx[g], yy, span, 1, px_ink);
            px_box(gx[g] + taper, yy - 1, span - 2 * taper, 1, px_ink);
            px_box(gx[g] + taper, yy + 1, span - 2 * taper, 1, px_ink);
        }
        if (jit) {
            px_line(gx[g] - jit, yy, gx[g] - 1, yy, px_ink, 2);
            px_line(gx[g] + span, yy, gx[g] + span - 1 + jit, yy, px_ink, 2);
        }
        span = span0;
    }
    fmt_int(b, n);
    str_cpy(b + str_len(b), " GRAINS", 8);
    if (f == 1u)
        px_tag(1, DLBL - 1, PXF_3, b, px_ink, px_bg);
    else
        px_text(2, DLBL, PXF_3, b, px_ink);
    if (f >= 4u && f < 12u)
        vz_ktag(118, DLBL - 1, &DEV_P[DEV_GRAIN][f], v[f]);
    else
        px_text(118 - px_text_w(PXF_3, tape_name(sys.sel)), DLBL, PXF_3, tape_name(sys.sel), px_dim);   /* its source */
}

/* --------------------------------------------------------- RESONATOR --- */
/* the strings' partials above PTCH, in 1/16 semitones: SCAL HARM is the root's harmonics (12 log2 k); MAJ, MIN and
 * PEN are their scale's chord tones (1 3 5, or the pentatonic's notes) stacked over three octaves */
static const int16_t PART_16[4][24] = {
    {0, 192, 304, 384, 446, 496, 539, 576, 609, 638, 664, 688, 710, 731, 750, 768, 785, 801, 816, 830, 843, 856, 869,
     880},
    {0, 64, 112, 192, 256, 304, 384, 448, 496, 576},
    {0, 48, 112, 192, 240, 304, 384, 432, 496, 576},
    {0, 32, 64, 112, 144, 192, 224, 256, 304, 336, 384, 416, 448, 496, 528, 576}};
static const uint8_t PART_N[4] = {24, 10, 10, 16};

/* x as a pitch: MIDI 28 (E1, 41 Hz) .. 124 (8 octaves), in 1/16 semitones */
static int32_t reso_n16(int32_t x) { return 28 * 16 + (x - DX0) * 96 * 16 / DW; }

/* the filter before the strings (CUT, RES, SLOP) at pitch n16, 0..1000 (+ the resonance's peak, up to 2000):
 * CUT's index is about a semitone a step from 30 Hz (MIDI 23) */
static int32_t reso_filter(const int16_t *v, int32_t n16)
{
    int32_t c16 = (23 + v[4]) * 16, d = (n16 - c16) / 16, pk = v[5] * 10 * 64 / (64 + d * d), g;
    switch (v[6]) {
    case 1: g = 1000 - (d < 0 ? -d : d) * 40; break;                   /* BP: 6 dB an octave each side */
    case 2: g = d >= 0 ? 1000 : 1000 + d * 80; break;                  /* HP */
    default: g = d <= 0 ? 1000 : 1000 - d * 80; break;                 /* LP: 12 dB an octave */
    }
    return clamp(g, 0, 1000) + pk;
}

/* peaks at the partials; the peak width narrows as DEC rises; TONE keeps the upper partials (dark at 0); the filter
 * shapes what reaches them; WET blends with the flat dry line (35 %); 0..1000 */
static int32_t reso_at(const int16_t *v, int32_t x)
{
    int32_t n16 = reso_n16(x), width16 = 4 + (100 - v[1]) * 28 / 100, best = 0, k, sc = clamp(v[7], 0, 3);
    for (k = 0; k < PART_N[sc]; k++) {
        int32_t d = n16 - (v[0] * 16 + PART_16[sc][k]), amp = 1000 - k * (100 - v[2]) * 9, pk;
        if (amp <= 0)
            break;
        if (d < -width16 * 6 || d > width16 * 6)
            continue;
        d = d < 0 ? -d : d;
        pk = amp * width16 * width16 / (width16 * width16 + d * d);   /* 1 / (1 + (d / w)^2) */
        if (pk > best)
            best = pk;
    }
    best = clamp(best * reso_filter(v, n16) / 1000, 0, 1000);
    return (best * v[3] + 350 * (100 - v[3])) / 100;
}

static void viz_reso(const int16_t *v, uint32_t f)
{
    static const char *const NAME[8] = {"PTCH", "DEC", "TONE", "WET", "CUT", "RES", "SLOP", "SCAL"};
    int32_t x, k, py = 0, top = DY0 + 6, base = DY1 + 2, sc = clamp(v[7], 0, 3);
    px_line(DX0, base - 350 * (base - top) / 1000, DX1, base - 350 * (base - top) / 1000, px_dim, 2);   /* dry */
    px_line(DX0, base + 1, DX1, base + 1, px_dim, 2);
    if (v[4] < 127 || v[5] || v[6] || f == 4u || f == 5u || f == 6u)   /* the filter: dotted, once it does anything */
        for (x = DX0; x <= DX1; x += 2)
            px_dot(x, base - clamp(reso_filter(v, reso_n16(x)), 0, 1000) * (base - top) / 1000, px_ink);
    for (x = DX0; x <= DX1; x++) {
        int32_t y = base - reso_at(v, x) * (base - top) / 1000;
        if (x > DX0)
            px_line(x - 1, py, x, y, px_ink, 1);
        py = y;
    }
    for (k = 0; k < PART_N[sc]; k++) {                                 /* a node on each partial still sounding */
        int32_t hx = DX0 + (v[0] * 16 + PART_16[sc][k] - 28 * 16) * DW / (96 * 16);
        if (hx > DX1 - 1 || 1000 - k * (100 - v[2]) * 9 <= 0 ||
            (k && hx - (DX0 + (v[0] * 16 + PART_16[sc][k - 1] - 28 * 16) * DW / (96 * 16)) < 3))
            break;                                                     /* (closer than 3 dots: the rest are a blur) */
        vz_node(hx, base - reso_at(v, hx) * (base - top) / 1000, k == 0);
    }
    {   /* PTCH: its note over its node */
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
    vz_label((DX0 + DX1) / 2, f >= 1u && f < 8u ? NAME[f] : N_RSCAL[sc], f >= 1u && f < 8u);
    if (f >= 4u && f < 8u)
        vz_ktag(118, DY0 - 1, &DEV_P[DEV_RESO][f], v[f]);
}

/* ------------------------------------------------------------- COLOR --- */
/* DRIV 0..100 -> gain 1..16 into the soft clip; CRSH 0..100 takes, by CMOD, 16..2 bits (BIT), a hold of 1..12
 * samples (RATE), or both; NOIS: noise riding the signal's envelope, which falls over NDEC after the sound; NTON: a
 * one-pole filter on that noise (- dark .. + bright); TILT: the low end against the high (on a sine: nothing to
 * draw, so the inset shows it); WET: the dry and the coloured signal; LVL: the output in dB */
static int32_t color_shape(int32_t x, const int16_t *v)             /* Q15 in -> Q15 out (before WET, LVL) */
{
    int32_t g = 1 + v[0] * 15 / 100, y = softclip(x * g) * 32767 / softclip(32767 * g);
    int32_t bits = v[6] == 1 ? 16 : 16 - v[1] * 14 / 100, q = 1 << (16 - bits);
    return q > 1 ? (y / q) * q : y;
}

static int32_t color_out(int32_t x, int32_t shaped, const int16_t *v)   /* WET, then LVL */
{
    return (x + (shaped - x) * v[7] / 100) * db_x1000(v[8]) / 1000;
}

static void viz_color(const int16_t *v, uint32_t f)
{
    static const char *const NAME[9] = {"DRIV", "CRSH", "NOIS", "TILT", "NDEC", "NTON", "CMOD", "WET", "LVL"};
    int32_t bx = DX0, bw = 32, wx = DX0 + 37, ww = DX1 - wx, i, py = 0, held = 0;
    int32_t hold = v[6] >= 1 ? 1 + v[1] * 11 / 100 : 1;              /* CMOD RATE or BOTH: hold a value */
    int32_t fall = 1000 - 2000 / (2 + (int32_t)TIME_MS_X10[v[4] & 127] / 40), env = 0;   /* NDEC, per dot x1000 */
    int32_t lp = 0, a = 8 + (v[5] + 100) * 75 / 200, cy = DMID + 1, amp = DH / 2;   /* NTON: the filter's step */
    px_frame(bx, DY0, bw, DH + 2, px_dim, 2);                          /* the inset */
    if (f == 5u) {                                                     /* the noise filter's response */
        int32_t pyc = 0;
        for (i = 0; i < bw - 4; i++) {
            int32_t fr = i * 100 / (bw - 4), gg = 100 * a / (a + fr + 1), y = DY1 - 2 - gg * (DH - 6) / 100;
            if (i)
                px_line(bx + 1 + i, pyc, bx + 2 + i, y, px_ink, 1);
            pyc = y;
        }
    } else if (f == 3u) {                                              /* TILT: one line, leaning */
        px_line(bx + 2, cy, bx + bw - 3, cy, px_dim, 2);
        px_line(bx + 2, cy + v[3] * (amp - 4) / 100, bx + bw - 3, cy - v[3] * (amp - 4) / 100, px_ink, 1);
    } else if (f == 2u || f == 4u) {                                   /* the noise: a hit, and the noise falling */
        vz_seed = 99u;                                                 /* .. after it over NDEC */
        for (i = 2; i < bw - 2; i++) {
            int32_t e = i < 6 ? 1000 : env * fall / 1000, k, n = (e * (4 + v[2] * 2) / 1000 + 9) / 10;
            env = e;
            for (k = 0; k < n; k++)
                px_dot(bx + i, DY1 - 1 - (int32_t)(vz_rand() % (uint32_t)(1 + e * (DH - 3) / 1000)), px_ink);
        }
    } else {                                                           /* the transfer curve */
        int32_t pyc = 0;
        px_line(bx + 2, cy, bx + bw - 3, cy, px_dim, 2);
        for (i = 0; i < bw - 4; i++) {
            int32_t x = (i * 2 - (bw - 5)) * 32767 / (bw - 5);
            int32_t y = cy - clamp(color_out(x, color_shape(x, v), v), -32767, 32767) * (amp - 3) / 32767;
            if (i)
                px_line(bx + 1 + i, pyc, bx + 2 + i, y, px_ink, 1);
            pyc = y;
        }
    }
    px_line(wx, cy, DX1, cy, px_dim, 2);
    vz_seed = 777u;
    env = 0;
    for (i = 0; i <= ww; i++) {                                        /* two cycles of a sine: in dotted, out solid */
        int32_t s = sine_i((uint32_t)i * (0xFFFFFFFFu / (uint32_t)ww) * 2u), y, yi, nz, e;
        if (i % hold == 0)
            held = color_shape(s, v);                                  /* CMOD RATE: hold a value */
        e = s < 0 ? -s : s;
        env = e > env ? e : env * fall / 1000;                         /* NDEC: the noise's envelope */
        nz = ((int32_t)(vz_rand() & 0xFFFF) - 32768) * v[2] / 100;      /* NOISE, through NTON */
        lp += (nz - lp) * a / 100;
        y = color_out(s, held + (lp * env >> 16), v);
        y = cy - clamp(y, -32767, 32767) * (amp - 1) / 32767;
        yi = cy - s * (amp - 1) / 32767;
        if (i) {
            if (i % 2 == 0)
                px_dot(wx + i, yi, px_dim);
            px_line(wx + i - 1, py, wx + i, y, px_ink, 1);
        }
        py = y;
    }
    vz_label(bx + bw / 2, f < 9u ? NAME[f] : "CURVE", f < 9u);
    vz_label(wx + ww / 2, "IN : OUT", 0);
    if (f >= 4u && f < 9u)
        vz_ktag(118, DY0 - 1, &DEV_P[DEV_COLOR][f], v[f]);
}

/* ------------------------------------------------------------- SPACE --- */
/* A sonar seen from above, flattened to fit the panel: the dry hit at the centre, the sound going out as rings.
 *   the delay   each echo a ring, its radius its time (TIME apart), there while it's still heard (FDBK, TONE's loss
 *               each pass, DLY's level): the first solid, the later ones dotted, rings closer than 3 dots merged;
 *               on each a notch as long as the echo is loud. SPRD sends the
 *               notches alternately left and right, the way a ping-pong delay throws echoes across: at 0 they sit
 *               at 12 o'clock.
 *   the reverb  VERB roughens each ring's 1-dot stroke and bleeds dots outward from it; the tail itself is a
 *               dither over the disc as dense as the reverb is loud at that distance: rising over SIZE (10..90 ms),
 *               falling over DEC (0.2..4.2 s), starting after PRE. Long and loud, it turns the disc into one
 *               blur of dots and eats into the rings, the notches lost in it.
 * The outermost ring is SP_W ms out. */
#define SP_W 1500
static int32_t isqrt(int32_t n);

/* the reverb's level ms after the hit, 0..1000 (squared, as the ear hears a tail fall) */
static int32_t sp_tail(const int16_t *v, int32_t ms)
{
    int32_t rise = 10 + v[2] * 80 / 100, len = 200 + v[3] * 40, e;
    ms -= v[8];                                                        /* PRE */
    if (ms < 0)
        return 0;
    e = ms < rise ? ms * 1000 / rise : 1000 - (ms - rise) * 1000 / len;
    return e <= 0 ? 0 : e * e / 1000;
}

/* the point at normalised radius r (0..1000 of the disc) and angle a (1/64 turn, 0 = 12 o'clock) */
#define SP_RX 44                     /* the disc: 88 x 36 dots round (60, 18) */
#define SP_RY 18
static int32_t sp_x(int32_t r, int32_t a) { return 60 + px_sin(a) * SP_RX / 1000 * r / 1000; }
static int32_t sp_y(int32_t r, int32_t a) { return 18 - px_cos(a) * SP_RY / 1000 * r / 1000; }

static void viz_space(const int16_t *v, uint32_t f)
{
    int32_t t, k, a, x, y, g = 32767, n = 0, prev = -1000, tl = 100 - (v[6] < 0 ? -v[6] : v[6]) / 5;
    int32_t jag = v[5] * 20 / 100, bleed = v[5] * 3 / 100, sh = v[7] * 16 / 100;   /* (jag: of the radius, 1/1000) */
    vz_seed = 4242u;
    for (t = v[0], k = 0; t <= SP_W; t += v[0], k++) {                 /* the echoes */
        int32_t r = t * 1000 / SP_W, lvl, px0 = 0, py0 = 0, len, mid;
        g = k ? g * v[1] / 100 * tl / 100 : 26000;
        lvl = g * v[4] / 100;
        if (lvl < 1200)
            break;
        n++;
        if ((r - prev) * SP_RY / 1000 >= 3) {                          /* its ring (closer than 3 dots: merged); the */
            for (a = 0; a <= 64; a++) {                                /* first solid, the later ones dotted */
                int32_t rr = r + (jag && a < 64 ? (int32_t)(vz_rand() % (uint32_t)(jag + 1)) : 0);
                int32_t xx = sp_x(rr, a), yy = sp_y(rr, a);
                if (a)
                    px_line(px0, py0, xx, yy, px_ink, k ? 2 : 1);
                px0 = xx;
                py0 = yy;
            }
            prev = r;
        }
        for (a = 0; a < bleed * 3; a++) {                              /* VERB: dots bled outward from it */
            int32_t aa = (int32_t)(vz_rand() % 64u), d = 1 + (int32_t)(vz_rand() % (uint32_t)(bleed + 1));
            px_dot(sp_x(r, aa) + px_sin(aa) * d / 1000, sp_y(r, aa) - px_cos(aa) * d / 1000, px_ink);
        }
        len = 1 + lvl * 3 / 26000;                                     /* the notch: 2 dots thick, as long as loud */
        mid = !sh ? 0 : (k & 1) ? sh : 64 - sh;
        for (a = (mid - len) * 4; a <= (mid + len) * 4; a++) {         /* (quarter steps: no gaps on wide rings) */
            int32_t a0 = a >> 2, q = a & 3;
            int32_t xx = sp_x(r, a0) + (sp_x(r, a0 + 1) - sp_x(r, a0)) * q / 4;
            int32_t yy = sp_y(r, a0) + (sp_y(r, a0 + 1) - sp_y(r, a0)) * q / 4;
            px_box(xx - 1, yy - 1, 2, 2, px_ink);
        }
    }
    if (v[5])                                                          /* the tail: a dither over the disc */
        for (y = 0; y <= 36; y++)
            for (x = DX0; x <= DX1; x++) {
                int32_t dx = (x - 60) * 1000 / SP_RX, dy = (y - 18) * 1000 / SP_RY, rho2 = dx * dx + dy * dy, p, h;
                if (rho2 > 1000 * 1000)
                    continue;
                p = sp_tail(v, isqrt(rho2) * SP_W / 1000) * v[5] / 100 * 4 / 10;   /* 0..400 of 1000 */
                h = (int32_t)(((uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u) * 2654435761u >> 22) % 1000;
                if (h < p)
                    px_dot(x, y, px_ink);
                else if (p > 250 && h < 2 * p - 250)                   /* loud and long: it eats into the rings */
                    px_dot(x, y, px_bg);
            }
    px_box(59, 17, 3, 3, px_ink);                                      /* the dry hit */
    px_dot(60, 18, px_bg);
    {
        char b[12] = "ECHO ";
        fmt_int(b + 5, n);
        vz_label(16, n ? b : "NO ECHO", f <= 1u || f == 4u || f == 7u);
    }
    vz_label(104, "TAIL", f == 2u || f == 3u || f == 5u || f == 8u);
    if (f >= 4u && f < 9u)
        vz_ktag(118, DLBL - 1, &DEV_P[DEV_SPACE][f], v[f]);
}

/* ------------------------------------------------------------- MODS --- */
/* The modulators share the S-4's placement knobs: AMT scales the shape, OFS moves it up or down, PHAS starts it
 * later in its cycle, SPRD is the right channel's phase against the left (drawn as a dotted second trace). A
 * page 2 or 3 knob you turn is tagged with its value at the right of the label row. */
static int32_t vz_place(int32_t y, int32_t amt, int32_t ofs)         /* Q15 -> Q15: AMT, then OFS */
{
    return clamp(y * amt / 100 + ofs * 327, -32767, 32767);
}

static int32_t isqrt(int32_t n)                                       /* floor(sqrt(n)), n >= 0 */
{
    int32_t x = n, y = (n + 1) / 2;
    if (n < 2)
        return n;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

/* The LFO's random steps: a loop of LEN values (Q15), and the next time round, drifted by VAR. One table for
 * both RND's steps and, on the other shapes, each cycle's level (VAR's drift repeats every LEN cycles). */
static void lfo_rand(const int16_t *v, int32_t *now, int32_t *next)
{
    int32_t k;
    vz_seed = 4242u;
    for (k = 0; k < 16; k++)
        now[k] = ((int32_t)(vz_rand() % 2001u) - 1000) * 32;
    for (k = 0; k < 16; k++)
        next[k] = clamp(now[k] + ((int32_t)(vz_rand() % 2001u) - 1000) * 32 * v[6] / 100, -32767, 32767);
}

/* the LFO at phase q (0..999 of a cycle), Q15: the shape (SIN TRI SQR SAW, or RND's step from rnd[]), SKEW's
 * warped phase, FOLD, CURV */
static int32_t lfo_at(const int16_t *v, int32_t q, const int32_t *rnd)
{
    int32_t sk = clamp(500 + v[2] * 5, 50, 950), sh = clamp(v[1], 0, 4), n = clamp(v[7], 1, 16), y, a;
    q = q < sk ? q * 500 / sk : 500 + (q - sk) * 500 / (1000 - sk);  /* SKEW: squashed to one side */
    if (sh == 0)
        y = sine_i((uint32_t)q * 4294967u);
    else if (sh == 1)
        y = q < 250 ? q * 131 : q < 750 ? (500 - q) * 131 : (q - 1000) * 131;
    else if (sh == 2)
        y = q < 500 ? 30000 : -30000;
    else if (sh == 3)
        y = (q - 500) * 65;                                           /* saw: a ramp up and the drop */
    else
        y = rnd[clamp(q * n / 1000, 0, n - 1)];                       /* RND: LEN held steps a cycle */
    y = y * (100 + v[3] * 3) / 100;                                   /* FOLD: overdrive, then fold back */
    while (y > 32767 || y < -32767)
        y = y > 0 ? 65534 - y : -65534 - y;
    a = y < 0 ? -y : y;                                               /* CURV: + narrows the curves, - widens them */
    if (v[4] > 0)
        a = (a * (100 - v[4]) + (a * a / 32767) * v[4]) / 100;
    else if (v[4] < 0)
        a = (a * (100 + v[4]) + isqrt(a * 32767) * (-v[4])) / 100;
    return y < 0 ? -a : a;
}

static void viz_wave(const int16_t *v, uint32_t f)
{
    /* Two cycles from PHAS. SMTH slews what's drawn (RND's steps glide); VAR: the next time round, dotted (RND: the
     * loop's steps drifted; the other shapes: each cycle's level drifted, repeating every LEN cycles). FADE fades
     * the first cycle in; SYNC BPM puts the beat ticks on the line; TRIG KEY marks the key that restarts it; SPRD's
     * right channel, dim. AMT and OFS place it all. */
    static const char *const NAME[5] = {"SINE", "TRIANGLE", "SQUARE", "SAW", "RANDOM"};
    int32_t i, py = DMID, sh = clamp(v[1], 0, 4), fade = v[14] * DW / 200, half = DH / 2 - 1, now[16], nxt[16];
    int32_t sm = 0, smn = 0, smr = 0, k = 1000 - v[5] * 9, n = clamp(v[7], 1, 16);
    lfo_rand(v, now, nxt);
    px_line(DX0, DMID, DX1, DMID, px_dim, 2);
    if (v[12])
        for (i = 0; i <= 8; i++)
            px_line(DX0 + i * DW / 8, DMID - 1, DX0 + i * DW / 8, DMID + 1, px_ink, 1);
    if (v[13]) {                                                       /* a key going down, at the start */
        px_box(DX0, DY0 - 1, 3, 4, px_ink);
        px_line(DX0 + 1, DY0 + 3, DX0 + 1, DY1, px_ink, 3);
    }
    for (i = 0; i <= DW; i++) {
        int32_t ph = i * 2000 / DW + v[10] * 1000 / 360, p = ph % 1000, cyc = ph / 1000, y, yn, yr;
        if (sh == 4) {
            y = lfo_at(v, p, now);
            yn = lfo_at(v, p, nxt);
        } else {                                                       /* VAR on a shape: each cycle's level */
            int32_t a0 = now[cyc % n] < 0 ? -now[cyc % n] : now[cyc % n], a1 = nxt[cyc % n] < 0 ? -nxt[cyc % n] : nxt[cyc % n];
            int32_t lv = 32767 - a0 * v[6] / 100, ln = 32767 - a1 * v[6] / 100;
            y = lfo_at(v, p, now);
            yn = y * (ln / 64) / 512;
            y = y * (lv / 64) / 512;
        }
        yr = lfo_at(v, (p + v[11] * 5) % 1000, now);
        if (i == 0) {
            sm = y;
            smn = yn;
            smr = yr;
        }
        sm += (y - sm) * k / 1000;                                     /* SMTH: a one-pole slew */
        smn += (yn - smn) * k / 1000;
        smr += (yr - smr) * k / 1000;
        y = sm;
        yn = smn;
        yr = smr;
        if (i < fade) {
            y = y * i / fade;
            yn = yn * i / fade;
            yr = yr * i / fade;
        }
        y = DMID - vz_place(y, v[8], v[9]) * half / 32767;
        if (v[6] && (i % 3) == 0)
            px_dot(DX0 + i, DMID - vz_place(yn, v[8], v[9]) * half / 32767, px_ink);
        if (v[11] && (i & 1) == 0)
            px_dot(DX0 + i, DMID - vz_place(yr, v[8], v[9]) * half / 32767, px_dim);
        if (i)
            px_line(DX0 + i - 1, py, DX0 + i, y, px_ink, 1);
        py = y;
    }
    vz_label(DX0 + 14, NAME[sh], f == 1u);
    if (f == 2u || f == 3u)
        vz_label(DX1 - 10, f == 2u ? "SKEW" : "FOLD", 1);
    if (f >= 4u && f < NPK)
        vz_ktag(118, DLBL - 1, &ME_P[ME_WAVE][f], v[f]);
}

/* t (0..1000) along a stage bent by its curve c (-100..100): 0 straight, + sags (exponential), - bows (log) */
static int32_t vz_bend(int32_t t, int32_t c)
{
    return t - c * t / 1000 * (1000 - t) / 400;
}

static void viz_adsr(const int16_t *v, uint32_t f)
{
    /* the times (0..127 on TIME_MS_X10: 1 ms .. 10 s) on a square-root scale so short and long both read; each
     * stage bent by its curve (ACRV DCRV RCRV); VEL: the softest key's envelope, dotted; LOOP: attack and decay
     * coming round again, dotted; SPRD: the right channel, dotted, later; AMT, OFS place it */
    int32_t a = (int32_t)TIME_MS_X10[v[0] & 127], d = (int32_t)TIME_MS_X10[v[1] & 127], r = (int32_t)TIME_MS_X10[v[3] & 127];
    int32_t sa = 1, sd = 1, sr = 1, tot, xs[5], ys[5], k, x, base = DY1, top = DY0 + 2, w = DW - 4, py = 0;
    static const char *const L[4] = {"ATK", "DEC", "SUS", "REL"};
    static const uint8_t CRV[4] = {4, 5, 0xFF, 6};                     /* each stage's curve knob (SUS holds) */
    while (sa * sa < a) sa++;
    while (sd * sd < d) sd++;
    while (sr * sr < r) sr++;
    tot = sa + sd + sr + (sa + sd + sr) / 3;                            /* (a sustain plateau a third as long) */
    xs[0] = DX0 + 2;
    xs[1] = xs[0] + sa * w / tot;
    xs[2] = xs[1] + sd * w / tot;
    xs[3] = xs[2] + (sa + sd + sr) / 3 * w / tot;
    xs[4] = DX1 - 2;
    ys[0] = 0;                                                         /* levels 0..1000, placed below */
    ys[1] = 1000;
    ys[2] = ys[3] = v[2] * 10;
    ys[4] = 0;
    px_line(DX0, base + 1, DX1, base + 1, px_dim, 2);
    for (k = 0; k < 4; k++)                                            /* the envelope, stage by stage */
        for (x = xs[k]; x <= xs[k + 1]; x++) {
            int32_t span = xs[k + 1] - xs[k], t = span ? (x - xs[k]) * 1000 / span : 1000, lv, yy, c;
            c = CRV[k] == 0xFF ? 0 : v[CRV[k]];
            lv = ys[k] + (ys[k + 1] - ys[k]) * vz_bend(t, ys[k + 1] > ys[k] ? -c : c) / 1000;
            lv = clamp(lv * v[10] / 100 + v[11] * 10, 0, 1000);
            yy = base - lv * (base - top) / 1000;
            if (v[8] && (x & 1) == 0)                                  /* VEL: the softest key */
                px_dot(x, base - lv * (100 - v[8]) / 100 * (base - top) / 1000, px_ink);
            if (v[7] && (x & 1) == 0 && x + v[7] * 8 / 100 <= DX1)      /* SPRD: the right channel, later */
                px_dot(x + v[7] * 8 / 100, yy, px_dim);
            if (x > xs[0])
                px_line(x - 1, py, x, yy, px_ink, 1);
            py = yy;
        }
    if (v[9]) {                                                        /* LOOP: attack and decay again */
        int32_t sy = base - clamp(ys[2] * v[10] / 100 + v[11] * 10, 0, 1000) * (base - top) / 1000;
        int32_t lx[3] = {xs[2], xs[2] + (xs[1] - xs[0]), xs[2] + (xs[2] - xs[0])}, ly[3] = {sy, top, sy};
        vz_poly(lx, ly, 3, px_ink, 2);
    }
    for (k = 1; k < 4; k++) {                                          /* nodes, and drop lines to the floor */
        int32_t ny = base - clamp(ys[k] * v[10] / 100 + v[11] * 10, 0, 1000) * (base - top) / 1000;
        px_line(xs[k], ny + 2, xs[k], base, px_dim, 2);
        vz_node(xs[k], ny, 0);
    }
    for (k = 0; k < 4; k++)                                            /* the stages, named under their stretch */
        vz_label((xs[k] + xs[k + 1]) / 2 + 1, L[k], f == (uint32_t)k || f == (uint32_t)CRV[k]);
    if (f >= 4u && f < NPK)
        vz_ktag(118, DY0 - 1, &ME_P[ME_ADSR][f], v[f]);
}

/* ------------------------------------------------------------- SYNTH --- */
/* The page shown decides the picture (each page is one part of the voice):
 *   OSC     three cycles of the two oscillators: the mix solid, the second one alone dotted, drifting ahead by DTUN
 *           (exaggerated, so a few cents show), NOIS as grit around the line
 *   FILTER  the low-pass's response over the audible range (log, CUTOFF_HZ's), its peak by RES; dotted, where the
 *           envelope at its top takes it (ENV); KTRK's arrow: where it sits an octave up the keys
 *   AMP     the envelope, drawn as the ADSR modulator's
 *   VOICE   the keys' note range (OCT and TUNE), the voices that play at once, the glide between two notes, drive */
static int32_t vz_osc(uint32_t w, int32_t p)          /* SYNTH's wave at phase p (64 a cycle), x1000 */
{
    return w == 4u ? ((p & 63) < 16 ? 1000 : -1000) : px_wave(w, p);
}

/* the low-pass's level in dB x10 at index i of the cutoff scale (about a semitone a step), cutoff at c, RES 0..100 */
static int32_t vz_lp_db10(int32_t i, int32_t c, int32_t res)
{
    int32_t d = i - c, pk = res * 18;                /* the peak: up to 18 dB */
    return pk * 16 / (16 + d * d) - (d > 0 ? d * 10 : 0);   /* 12 dB an octave above it (1 dB a step) */
}

/* the same for a filter of TYPE type: 0 low-pass, 1 band-pass (6 dB an octave each side), 2 high-pass */
static int32_t vz_filt_db10(int32_t i, int32_t c, int32_t res, int32_t type)
{
    int32_t d = i - c;
    if (type == 1)
        return res * 18 * 16 / (16 + d * d) - (d < 0 ? -d : d) * 5;
    if (type == 2)
        return res * 18 * 16 / (16 + d * d) - (d < 0 ? -d * 10 : 0);
    return vz_lp_db10(i, c, res);
}

/* a filter page: the response over the cutoff scale (CUT c, RES res, TYPE type), dotted where the envelope at its
 * top takes it (ENV env, -100..100, 96 steps at full), KTRK's arrow (ktrk %: where it sits an octave up the keys);
 * fi: the page's last-turned knob (0..3, 0xFF none), page: its four knobs (for the tag), knobs CUT RES TYPE|- ENV */
static void vz_filter(int32_t c, int32_t res, int32_t type, int32_t env, int32_t ktrk, uint32_t fi,
                      const pdesc_t *page, const int16_t *pv, uint32_t env_k)
{
    int32_t x, ce = clamp(c + env * 96 / 100, 0, 127), ck = clamp(c + ktrk * 12 / 100, 0, 127);
    int32_t py = 0, z = DY0 + 13;                     /* 0 dB at z; 18 dB of peak above it, 24 below */
    px_line(DX0, z, DX1, z, px_dim, 2);
    for (x = DX0; x <= DX1; x++) {
        int32_t i = (x - DX0) * 127 / DW;
        int32_t yy = clamp(z - vz_filt_db10(i, c, res, type) * 9 / 100, DY0, DY1);
        int32_t ye = clamp(z - vz_filt_db10(i, ce, res, type) * 9 / 100, DY0, DY1);
        if (x > DX0) {
            px_line(x - 1, py, x, yy, px_ink, 1);
            if (env && (x & 1) == 0)
                px_dot(x, ye, px_ink);
        }
        py = yy;
    }
    {
        int32_t cx = DX0 + c * DW / 127, ex = DX0 + ce * DW / 127, kx = DX0 + ck * DW / 127;
        vz_node(cx, clamp(z - vz_filt_db10(c, c, res, type) * 9 / 100, DY0 + 1, DY1 - 1), 1);
        px_line(cx, DY1 - 3, cx, DY1, px_ink, 1);
        if (env && ex != cx) {
            vz_node(ex, clamp(z - vz_filt_db10(ce, ce, res, type) * 9 / 100, DY0 + 1, DY1 - 1), 0);
            px_line(cx, DY1 - 1, ex, DY1 - 1, px_ink, 2);
        }
        if (ktrk && kx > cx + 2)                      /* KTRK: an octave up the keys */
            px_line(cx, DY1 + 1, kx, DY1 + 1, px_dim, 1);
        vz_label(cx, "CUT", fi == 0u);
        if (env && (ex - cx > 14 || cx - ex > 14))
            vz_label(ex, "ENV", fi == env_k);
    }
    if (fi < 4u && fi != 0u)
        vz_ktag(118, DY0 - 1, &page[fi], pv[fi]);
}

static void viz_synth(const int16_t *v, uint32_t f)
{
    int32_t x, k;
    if (ui.page == 0u) {
        uint32_t w = (uint32_t)clamp(v[SY_WAVE], 0, 4);
        int32_t py = 0, m2 = v[SY_MIX], dr = v[SY_DTUN];
        vz_seed = 77u;
        px_line(DX0, DMID, DX1, DMID, px_dim, 2);
        for (x = DX0; x <= DX1; x++) {
            int32_t p = (x - DX0) * 192 / DW, p2 = p + (x - DX0) * dr * 24 / (DW * 100);   /* 3 cycles; drift */
            int32_t a = vz_osc(w, p), b2 = vz_osc(w, p2), o = (a * (100 - m2) + b2 * m2) / 100, yy;
            yy = DMID - o * (DH / 2 - 2) / 1000;
            if (x > DX0)
                px_line(x - 1, py, x, yy, px_ink, 1);
            if (m2 && (x & 1) == 0)
                px_dot(x, DMID - b2 * (DH / 2 - 2) / 1000, px_dim);
            if (v[SY_NOIS] && (int32_t)(vz_rand() % 100u) < v[SY_NOIS] / 2)
                px_dot(x, yy + (int32_t)(vz_rand() % 7u) - 3, px_ink);
            py = yy;
        }
        vz_label(14, N_OSC[w], f == 0u);
        vz_label(45, "DTUN", f == 1u);
        vz_label(75, "MIX", f == 2u);
        vz_label(105, "NOIS", f == 3u);
    } else if (ui.page == 1u) {
        vz_filter(v[SY_CUT], v[SY_RES], 0, v[SY_ENV], v[SY_KTRK], f < NPK ? f % 4u : 0xFFu, SYN_P + 4, v + 4, 2u);
    } else if (ui.page == 2u) {
        int16_t a[NPK] = {0};
        a[0] = v[SY_ATK];
        a[1] = v[SY_DEC];
        a[2] = v[SY_SUS];
        a[3] = v[SY_REL];
        a[10] = 100;                                  /* (AMT 100, OFS 0: the envelope as it is) */
        viz_adsr(a, f < 4u ? f : 0xFFu);
    } else {
        static const char NOTE[12][3] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        int32_t lo = 12 * ((int32_t)track[sys.sel].octave + 1) + v[SY_TUNE], hi = lo + 15, nv = v[SY_VOIC];
        char b[16];
        lo = clamp(lo, 0, 127);
        hi = clamp(hi, 0, 127);
        str_cpy(b, NOTE[lo % 12], 3);                 /* the white keys' range: "C3-D#4" */
        fmt_int(b + str_len(b), lo / 12 - 1);
        str_cpy(b + str_len(b), "-", 2);
        str_cpy(b + str_len(b), NOTE[hi % 12], 3);
        fmt_int(b + str_len(b), hi / 12 - 1);
        px_text_c(0, 120, DY0 + 1, PXF_5, b, px_ink);
        for (k = 0; k < (int32_t)SYN_NV; k++)          /* VOIC: the voices, as many solid as play */
            if (k < nv)
                px_box(6, 14 + k * 6, 18, 4, px_ink);
            else
                px_frame(6, 14 + k * 6, 18, 4, px_dim, 2);
        {   /* GLID: from one note to the next, sloped by the glide's time (square root, as the envelope's) */
            int32_t t = (int32_t)TIME_MS_X10[v[SY_GLID] & 127], sq = 1, w;
            while (sq * sq < t)
                sq++;
            w = v[SY_GLID] ? clamp(sq * 22 / 317, 1, 22) : 0;
            px_line(34, 31, 40, 31, px_ink, 1);
            px_line(40, 31, 40 + w, 15, px_ink, 1);
            px_line(40 + w, 15, 62, 15, px_ink, 1);
            px_line(34, 15, 62, 15, px_dim, 2);
        }
        {   /* DRV: the transfer curve */
            int32_t g = 1000 + v[SY_DRV] * 20, py = 0;
            px_frame(68, 13, 20, 20, px_dim, 2);
            for (x = 0; x < 20; x++) {
                int32_t yy = 23 - px_clip((x - 10) * 100, g) * 9 / 1000;
                if (x)
                    px_line(68 + x - 1, py, 68 + x, yy, px_ink, 1);
                py = yy;
            }
        }
        fmt_int(b, v[SY_TUNE]);                       /* TUNE */
        if (v[SY_TUNE] > 0) {
            b[0] = '+';
            fmt_int(b + 1, v[SY_TUNE]);
        }
        str_cpy(b + str_len(b), "st", 3);
        px_text_c(92, 26, 19, PXF_5, b, px_ink);
        vz_label(15, "VOIC", f == 0u);
        vz_label(48, "GLID", f == 1u);
        vz_label(78, "DRV", f == 2u);
        vz_label(105, "TUNE", f == 3u);
    }
}

/* -------------------------------------------------------------- POLY --- */
/* SAMPLE: the sound (its blocks' peaks; named), lit from STRT on, where every note starts; the keys' range under it
 * (OCT, TUNE) and how fast the top and bottom keys read it; the voices as boxes. ENV: the envelope, as SYNTH's.
 * FILTER: the response, its TYPE, dotted where ENV takes it */
static void viz_poly(const int16_t *v, uint32_t f)
{
    static const char NOTE[12][3] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int32_t x, k;
    if (ui.page == 0u) {
        tape_view_t vw;
        int32_t sx = DX0 + v[PL_STRT] * DW / 100, mid = 11, lo, hi, nv = v[PL_VOIC];
        char b[24];
        tape_view_of(sys.sel, (uint32_t)clamp(v[PL_REEL], 0, (int32_t)(NREEL + USLOT_N)), &vw);
        for (x = DX0; x <= DX1; x++) {                                 /* the sound */
            uint32_t blk = vw.len ? (uint32_t)(x - DX0) * (vw.len / TAPE_BLK) / (uint32_t)(DW + 1) : 0;
            int32_t a = vw.len ? vw.peak[blk] * 9 / 255 : 0;
            px_box(x, mid - a, 1, 2 * a + 1, x >= sx ? px_ink : px_dim);
        }
        if (!vw.len)
            px_text_c(0, 120, mid - 2, PXF_3, "EMPTY", px_ink);
        px_box(sx, DY0, 1, 22, px_ink);                                /* STRT: where every note starts */
        for (k = 0; k < 3; k++)
            px_box(sx - 2 + k, DY0 + k, 5 - 2 * k, 1, px_ink);
        lo = clamp(12 * ((int32_t)track[sys.sel].octave + 1) + v[PL_TUNE], 0, 127);   /* the keys' range */
        hi = clamp(lo + 15, 0, 127);
        str_cpy(b, NOTE[lo % 12], 3);
        fmt_int(b + str_len(b), lo / 12 - 1);
        str_cpy(b + str_len(b), "-", 2);
        str_cpy(b + str_len(b), NOTE[hi % 12], 3);
        fmt_int(b + str_len(b), hi / 12 - 1);
        px_text(DX0, 26, PXF_3, b, f == 2u ? px_ink : px_dim);
        for (k = 0; k < (int32_t)POL_NV; k++)                          /* the voices */
            if (k < nv)
                px_box(DX1 - 30 + k * 8, 26, 6, 5, px_ink);
            else
                px_frame(DX1 - 30 + k * 8, 26, 6, 5, px_dim, 2);
        vz_label(DX0 + 14, tape_name_of(sys.sel, (uint32_t)clamp(v[PL_REEL], 0, (int32_t)(NREEL + USLOT_N))), f == 0u);
        if (sx > DX0 + 34 || f == 1u)                                  /* (clear of the name, unless just turned) */
            vz_label(clamp(sx, DX0 + 14, DX1 - 30), "STRT", f == 1u);
        vz_label(DX1 - 18, "VOIC", f == 3u);
        if (f == 2u)
            vz_ktag(78, DLBL - 1, &POL_P[PL_TUNE], v[PL_TUNE]);
    } else if (ui.page == 1u) {
        int16_t a[NPK] = {0};
        a[0] = v[PL_ATK];
        a[1] = v[PL_DEC];
        a[2] = v[PL_SUS];
        a[3] = v[PL_REL];
        a[10] = 100;
        viz_adsr(a, f >= 4u && f < 8u ? f - 4u : 0xFFu);
    } else {
        vz_filter(v[PL_CUT], v[PL_RES], clamp(v[PL_TYPE], 0, 2), v[PL_ENV], 0, f < NPK ? f % 4u : 0xFFu, POL_P + 8,
                  v + 8, 3u);
        px_text(DX0, DY0, PXF_3, N_SLOP[clamp(v[PL_TYPE], 0, 2)], px_ink);
    }
}

/* FOLLOW: the envelope of a sound: what the source track plays, from its tape's peaks, dim; GAIN
 * scales what goes in, RISE and FALL are how fast the envelope (solid) climbs and drops, HOLD samples it on the
 * tempo's divisions (steps), AMT and OFS place it, SPRD the right channel dotted */
static void viz_follow(const int16_t *v, uint32_t f)
{
    static const uint8_t HOLD_DOTS[5] = {0, 4, 7, 14, 28};            /* a division's width on this 2-bar panel */
    int32_t x, e = 0, held = 0, py = DY1, kr = clamp(3000 / (3 + v[2]), 15, 1000), kf = clamp(3000 / (3 + v[3]), 15, 1000);
    int32_t gain = db_x1000(v[1]), hd = HOLD_DOTS[clamp(v[4], 0, 4)], src = clamp(v[0], 0, 5);
    uint32_t t = src >= 1 && src <= 4 ? (uint32_t)(src - 1) : sys.sel;
    char b[12];
    px_line(DX0, DY1 + 1, DX1, DY1 + 1, px_dim, 2);
    for (x = DX0; x <= DX1; x++) {
        int32_t in = clamp(tape_peak_at(t, (x - DX0) * 1000 / DW) * gain / 1000, 0, 1000), out, yy;
        if ((x & 1) == 0)
            px_line(x, DY1, x, DY1 - in * (DH - 2) / 1000, px_dim, 1);   /* what it listens to */
        e += (in - e) * (in > e ? kr : kf) / 1000;
        if (!hd || (x - DX0) % hd == 0)
            held = e;
        out = clamp(held * v[5] / 100 + v[6] * 10, 0, 1000);
        yy = DY1 - out * (DH - 2) / 1000;
        if (v[7] && (x & 1) == 0 && x + v[7] * 8 / 100 <= DX1)
            px_dot(x + v[7] * 8 / 100, yy, px_ink);
        if (x > DX0)
            px_line(x - 1, py, x, yy, px_ink, 1);
        py = yy;
    }
    str_cpy(b, "SRC ", sizeof b);
    str_cpy(b + 4, N_SRC[src], 6);
    if (f == 0u)
        px_tag(1, DLBL - 1, PXF_3, b, px_ink, px_bg);
    else
        px_text(2, DLBL, PXF_3, b, px_ink);
    if (f >= 1u && f < NPK)
        vz_ktag(118, DLBL - 1, &ME_P[ME_FOLLOW][f], v[f]);
    else
        px_text(118 - px_text_w(PXF_3, tape_name(t)), DLBL, PXF_3, tape_name(t), px_dim);
}

static void viz_seq(const int16_t *v, uint32_t f)
{
    /* the 16 steps in four groups of four, each a bar as tall as its value (tp[].steps, 0..100) with a node on
     * top; the steps past LEN are their dotted slots only. SWING nudges the off-beats late; SLEW draws the glide
     * from each step's value into the next. The step numbers sit under each group; the last-turned knob is named
     * at the right of that row. */
    const int8_t *st = tp[sys.sel].steps[ui.slot];
    int32_t k, bw = 5, gap = 1, gg = 3, top = DY0 + 1, floor = DY1, h = floor - top, sw = v[3] * 2 / 100;
    int32_t px = 0, py = 0;
    px_line(DX0, floor + 1, DX1, floor + 1, px_dim, 2);
    for (k = 0; k < 16; k++) {
        int32_t x = DX0 + 3 + k * (bw + gap) + (k / 4) * gg + ((k & 1) ? sw : 0), on = k < v[0];
        int32_t y = floor - st[k] * h / 100;
        if (on) {
            px_frame(x, top, bw, h + 1, px_dim, 2);                    /* the step's range, dotted */
            if ((k * 37 + 11) % 100 < v[6])                            /* its value (PROB: the steps that won't */
                px_box(x + 1, y, bw - 2, floor - y + 1, px_ink);       /* play this time are hollow) */
            else
                px_frame(x + 1, y, bw - 2, floor - y + 1, px_ink, 1);
            px_box(x, y, bw, 1, px_ink);                               /* a cap as wide as the step */
            if (k + 1 == v[7]) {                                       /* STRT: a triangle over the first step */
                px_box(x, top - 2, bw, 1, px_ink);
                px_box(x + 1, top - 1, bw - 2, 1, px_ink);
            }
            if (v[2] && k)                                             /* SLEW: the glide from the last value */
                px_line(px, py, x + v[2] * (bw - 1) / 100, y, px_ink, 1);
            px = x + bw - 1;
            py = y;
        } else {
            px_line(x, floor, x + bw - 1, floor, px_dim, 1);
        }
        if ((k & 3) == 0) {
            char n[4];
            fmt_int(n, k + 1);
            px_text(x, DLBL, PXF_3, n, k < v[0] ? px_ink : px_dim);
        }
    }
    if (f < 4u) {
        static const char *const L[4] = {"LEN", "RATE", "SLEW", "SWNG"};
        px_tag(118 - px_text_w(PXF_3, L[f]) - 1, DLBL - 1, PXF_3, L[f], px_ink, px_bg);
    } else if (f < 8u) {
        vz_ktag(118, DLBL - 1, &ME_P[ME_SEQ][f], v[f]);
    } else if (v[4]) {                                                 /* not forward: the order, named */
        px_text(118 - px_text_w(PXF_3, ME_P[ME_SEQ][4].names[v[4]]), DLBL, PXF_3, ME_P[ME_SEQ][4].names[v[4]], px_ink);
    }
}

/* ------------------------------------------------------------- MIXER --- */
/* the selected track's channel strip (the mixer, both pages: the levels above, this below): its EQ and filter as
 * one response over 8 octaves (ch_resp, ui_px.c: what the mixer DSP will apply), a node on each shelf's corner and
 * on the filter's, and the pan as two speakers whose bars show each side's gain */
static void viz_channel(uint32_t f)
{
    const int16_t *ch = tp[sys.sel].ch;
    int32_t w = 88, x, py = 0, mid = 15, k, pl, pr;
    int on = ui.chan;                                  /* the labels invert only where the knobs set them */
    px_line(DX0, mid, DX0 + w, mid, px_dim, 2);       /* 0 dB */
    px_line(DX0, mid - 10, DX0 + w, mid - 10, px_dim, 4);   /* +12 */
    px_line(DX0, mid + 10, DX0 + w, mid + 10, px_dim, 4);   /* -12 */
    for (x = 0; x <= w; x++) {
        int32_t y = clamp(mid - ch_resp(ch, x, w + 1) * 10 / 120, DY0, DY1);
        if (x)
            px_line(DX0 + x - 1, py, DX0 + x, y, px_ink, 1);
        py = y;
    }
    for (k = 0; k < 2; k++) {                          /* the shelves' corners */
        int32_t cx = DX0 + w * (k ? 70 : 30) / 100;
        vz_node(cx, clamp(mid - ch_resp(ch, cx - DX0, w + 1) * 10 / 120, DY0, DY1), 0);
    }
    if (ch[CH_FILT]) {                                 /* the filter's corner, solid */
        int32_t c = ch[CH_FILT] < 0 ? w - (-ch[CH_FILT]) * w * 85 / 10000 : ch[CH_FILT] * w * 85 / 10000;
        vz_node(DX0 + c, clamp(mid - ch_resp(ch, c, w + 1) * 10 / 120, DY0, DY1), 1);
    }
    /* the pan: L and R, each side's gain as a bar (equal power: cos and sin of the place) */
    pl = px_cos((ch[CH_PAN] + 100) * 8 / 100) * 22 / 1000;
    pr = px_sin((ch[CH_PAN] + 100) * 8 / 100) * 22 / 1000;
    px_text(98, DY0, PXF_3, "L", px_ink);
    px_text(113, DY0, PXF_3, "R", px_ink);
    px_box(98, DY1 - pl, 3, pl, px_ink);
    px_box(113, DY1 - pr, 3, pr, px_ink);
    px_line(97, DY1 + 1, 117, DY1 + 1, px_dim, 2);
    px_box(106 + ch[CH_PAN] * 6 / 100, DY1 - 3, 3, 3, px_ink);   /* where it sits */
    vz_label(DX0 + w * 30 / 100 - 4, "LOW", on && f == 0u);
    vz_label(DX0 + w * 70 / 100 + 4, "HIGH", on && f == 1u);
    vz_label(DX0 + w / 2, "FILT", on && f == 2u);
    vz_label(107, "PAN", on && f == 3u);
    {   /* whose channel: the selected track, tagged (the fader with the border above) */
        char n[3] = {'T', (char)('1' + sys.sel), 0};
        px_tag(1, DLBL - 1, PXF_3, n, px_ink, px_bg);
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
        for (t = 0; t < NCH; t++)
            h = (h ^ (uint32_t)(tp[sys.sel].ch[t] + 32768)) * 16777619u;
        return h + ui.chan * 977u;
    }
    for (k = 0; k < NPK; k++) {                                        /* both pages: page 2 shows in the picture */
        const int16_t *v = ui.kind == FOCUS_SLOT ? tp[sys.sel].mod[ui.slot] : dev_v(sys.sel, ui.dev);
        h = (h ^ (uint32_t)(v[k] + 32768)) * 16777619u;
    }
    h += ui.page * 389u;
    if (ui.kind == FOCUS_SLOT && tp[sys.sel].engine[ui.slot] == ME_SEQ)   /* SEQ draws its step values */
        for (k = 0; k < 16u; k++)
            h = (h ^ (uint32_t)(uint8_t)tp[sys.sel].steps[ui.slot][k]) * 16777619u;
    if (ui.kind == FOCUS_DEV && ui.dev == DEV_SRC)                     /* the source; the keys' range: the octave */
        h = (h ^ (tp[sys.sel].src * 7u + track[sys.sel].octave * 131u)) * 16777619u;
    if (ui.kind == FOCUS_DEV && ui.dev == DEV_SRC && tp[sys.sel].src == SRC_POLY)   /* POLY on the track's tape */
        h = (h ^ (tape_ver[sys.sel] * 31u)) * 16777619u;
    if (ui.kind == FOCUS_DEV && ((ui.dev == DEV_SRC && tp[sys.sel].src == SRC_TAPE) || ui.dev == DEV_GRAIN))   /* the tape, the head */
        h = (h ^ (tape_ver[sys.sel] * 31u + (uint32_t)(tape_head(sys.sel) * DW / 1000 + 7) + sys.rec * 977u)) * 16777619u;
    if (ui.kind == FOCUS_SLOT && tp[sys.sel].engine[ui.slot] == ME_FOLLOW)
        for (t = 0; t < NTRK; t++)
            h = (h ^ tape_ver[t]) * 16777619u;
    if (ui.kind == FOCUS_DEV && ui.dev == DEV_GRAIN)                   /* GRAIN draws TAPE's loop window too */
        h = (h ^ (uint32_t)(tp[sys.sel].dev[DEV_SRC][0] << 8 | tp[sys.sel].dev[DEV_SRC][1])) * 16777619u;
    return h + (ui.kind == FOCUS_SLOT ? tp[sys.sel].engine[ui.slot] * 65537u : 0u);
}

static void draw_viz(void)
{
    uint32_t sig = viz_sig(), f = ui.last < 4u ? 4u * ui.page + ui.last : 0xFFu;
    const int16_t *v = ui.kind == FOCUS_SLOT ? tp[sys.sel].mod[ui.slot] : dev_v(sys.sel, ui.dev);   /* both pages */
    if (!ui.force && sig == ui.sig_viz)
        return;
    ui.sig_viz = sig;
    px_colors();
    cv_begin(240, VZ_H, px_bg);
    if (ui.msg_t) {
        viz_message();
    } else if (ui.view == VIEW_MIXER) {
        viz_channel(f);
    } else {
        if (ui.kind == FOCUS_SLOT) {
            switch (tp[sys.sel].engine[ui.slot]) {
            case ME_WAVE: viz_wave(v, f); break;
            case ME_ADSR: viz_adsr(v, f); break;
            case ME_FOLLOW: viz_follow(v, f); break;
            default: viz_seq(v, f); break;
            }
        } else {
            switch (ui.dev) {
            case DEV_SRC:
                if (tp[sys.sel].src == SRC_SYNTH)
                    viz_synth(v, f);
                else if (tp[sys.sel].src == SRC_POLY)
                    viz_poly(v, f);
                else
                    viz_tape(v, f);
                break;
            case DEV_GRAIN: viz_grain(v, f); break;
            case DEV_RESO: viz_reso(v, f); break;
            case DEV_COLOR: viz_color(v, f); break;
            default: viz_space(v, f); break;
            }
        }
    }
    cv_blit(0, UI_VIZ_Y);
}

/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: the visualization panel under the 4-value strip (y 124..215). One picture per view, drawn from the
 * parameter values (not from audio), so it is right the moment a knob turns and costs nothing while nothing
 * changes:
 *
 *   TAPE       the loop window on the tape (START, LENGTH), the playhead, SPEED's direction, DUB's depth
 *   GRAIN      the grain cloud: grains across the loop (DENS), their length (SIZE), their pitch (PITCH, the cycles
 *              inside each grain) and their place in the stereo field (SPREAD, up = left, down = right)
 *   RESONATOR  the response of the tuned feedback network over 8 octaves: peaks on ROOT's harmonics, as sharp
 *              as FDBK makes them, rolling off with DAMP, blended with the dry line by MIX
 *   COLOR      a sine through the device; the inset shows what the last-turned knob does: the transfer curve
 *              (DRIVE, CRUSH), the noise riding the envelope (NOISE) or the tone filter (TONE)
 *   SPACE      the dry hit, the echoes (TIME apart, falling by FDBK) and the reverb tail (SIZE, DECAY)
 *   MOD slots  the engine's shape in the slot's colour: WAVE, RANDOM, ADSR, SEQ (its 16 steps)
 *   MIXER      the four tracks: level, meter, mute, which one is focused
 *
 * The most recently turned knob of the page (ui.last) is drawn in the accent, and the panel's right-hand caption
 * names it with its value, so the picture says which part of it that knob moves. A modulator's shape keeps its slot's
 * colour (PRD 5: the colours are the slots' identity); there the caption alone names the knob. The DSP of each device will use
 * the same mappings as these pictures (the comments name them), so what is drawn is what is heard. Integer only:
 * no float on the device. */

#define VZ_H 92
#define VX0 8
#define VX1 232
#define VW (VX1 - VX0)
#define VY0 22                       /* the plot, below the caption line */
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

/* a vertical line, 1 px */
static void vz_vline(int32_t x, int32_t y0, int32_t y1, uint16_t c)
{
    if (y1 < y0) {
        int32_t t = y0;
        y0 = y1;
        y1 = t;
    }
    cv_rect(x, y0, 1, y1 - y0 + 1, c);
}

/* the caption: what the picture is (left), the last-turned knob and its value (right, in the accent) */
static void vz_caption(const char *what, uint32_t page_has_last)
{
    cv_text_on(VX0, 3, &AF_S, what, T_MID, T_BG);
    if (page_has_last && ui.last < 4u) {
        int16_t *vp;
        const pdesc_t *d = ui_page(ui.last, &vp);
        char b[24], val[12];
        const char *unit;
        param_format(d, *vp, val, &unit);
        str_cpy(b, d->label, sizeof b);
        str_cpy(b + str_len(b), " ", 2);
        str_cpy(b + str_len(b), val, 12);
        str_cpy(b + str_len(b), unit, 4);
        cv_text_r(VX1, 3, &AF_S, b, T_ACCENT, T_BG);
    }
}

/* -------------------------------------------------------------- TAPE --- */
static void viz_tape(const int16_t *v, uint32_t f)
{
    int32_t x0 = VX0 + v[0] * VW / 100, x1 = x0 + v[1] * VW / 100, sp = v[2], i, n;
    if (x1 > VX1)
        x1 = VX1;
    vz_caption("LOOP", 1);
    cv_rrect(VX0, VY0 + 6, VW, 36, 4, T_RAISE, T_BG);                /* the whole tape */
    for (i = VX0 + 4; i < VX1 - 4; i += 6)
        cv_rect(i, VY0 + 24, 3, 1, T_DIM);                             /* empty: a dotted line (phase 2: the wave) */
    cv_text_in(VX0, VY0 + 44, VW, &AF_S, "EMPTY TAPE", T_DIM, T_BG);   /* (phase 2: record, load a reel) */
    cv_rect(x0, VY0 + 6, x1 - x0, 36, T_TINT);                         /* the loop window */
    cv_rect(x0, VY0 + 2, 2, 44, vz_hi(f == 0u, T_THEME));             /* START */
    cv_rect(x1 - 2, VY0 + 2, 2, 44, vz_hi(f == 1u, T_THEME));         /* the end (LENGTH) */
    {   /* the playhead sits where the loop starts in the playing direction */
        int32_t ph = sp < 0 ? x1 - 4 : x0 + 3;
        cv_rect(ph, VY0 + 8, 1, 32, T_TEXT);
    }
    n = sp < 0 ? -sp : sp;                                             /* SPEED: chevrons, one per 50 % */
    n = n == 0 ? 0 : clamp(n / 50 + 1, 1, 4);
    {
        int32_t cx = (x0 + x1) / 2 - n * 5, cy = VY0 + 24;
        uint16_t c = vz_hi(f == 2u, T_THEME);
        if (!n) {                                                       /* stopped: a pause sign */
            cv_rect((x0 + x1) / 2 - 4, cy - 6, 3, 12, c);
            cv_rect((x0 + x1) / 2 + 2, cy - 6, 3, 12, c);
        }
        for (i = 0; i < n; i++) {
            int32_t x = cx + i * 10, d = sp < 0 ? -1 : 1, b = sp < 0 ? x + 6 : x;
            cv_line_t(b, cy - 6, b + 6 * d, cy, c, 2);
            cv_line_t(b, cy + 6, b + 6 * d, cy, c, 2);
        }
    }
    /* DUB: how much of the old take each pass keeps, a bar under the window */
    cv_rrect(x0, VY1 - 6, x1 - x0, 4, 2, T_RAISE, T_BG);
    if (v[3])
        cv_rrect(x0, VY1 - 6, (x1 - x0) * v[3] / 100 < 3 ? 3 : (x1 - x0) * v[3] / 100, 4, 2, vz_hi(f == 3u, T_THEME), T_RAISE);
}

/* ------------------------------------------------------------- GRAIN --- */
static void viz_grain(const int16_t *v, uint32_t f)
{
    /* DENS 0..100 -> 1..25 grains drawn; SIZE 5..500 ms -> 4..60 px; PITCH -> cycles inside a grain (2^(st/12));
     * SPREAD -> the stereo scatter (up = left) */
    int32_t n = 1 + v[1] * 24 / 100, w = 4 + (v[0] - 5) * 56 / 495, half = VH / 2 - 6, i;
    uint32_t inc = pitch_inc((uint32_t)(60 + v[2]) * 16u) / (pitch_inc(60u * 16u) / 64u);   /* 64 = unison */
    vz_caption("GRAIN CLOUD", 1);
    cv_rect(VX0, VMID, VW, 1, vz_hi(f == 3u, T_LINE));
    cv_text_on(VX0, VY0 - 2, &AF_S, "L", T_DIM, T_BG);
    cv_text_on(VX0, VY1 - 12, &AF_S, "R", T_DIM, T_BG);
    vz_seed = 12345u;
    for (i = 0; i < n; i++) {
        int32_t gx = VX0 + 12 + (int32_t)(vz_rand() % (uint32_t)(VW - 12 - w));
        int32_t gy = VMID + ((int32_t)(vz_rand() % 2001u) - 1000) * half * v[3] / 100000;
        int32_t k, py = gy;
        /* 2 cycles per grain at unison (inc 64), capped at one cycle per 3 px: no aliasing; no overflow
         * (at most 64 / 3 * 2^26 < 2^31) */
        uint32_t cyc = 2u * inc < (uint32_t)w * 64u / 3u ? 2u * inc : (uint32_t)w * 64u / 3u, ph = 0;
        uint32_t step = cyc * (67108864u / (uint32_t)w);
        uint16_t cl = vz_hi(f == 0u, T_THEME), cs = vz_hi(f == 2u, T_TEXT);
        for (k = 0; k <= w; k++) {                                     /* the window (a lens) and the sine in it */
            int32_t e = (4 * k * (w - k)) * 6 / (w > 0 ? w * w : 1), y;    /* 0..6 px, Hann-like */
            cv_pset(gx + k, gy - e, cl);
            cv_pset(gx + k, gy + e, cl);
            y = gy + (sine_i(ph) * e >> 15);
            if (k)
                cv_line(gx + k - 1, py, gx + k, y, cs);
            py = y;
            ph += step;
        }
    }
    if (f == 1u) {                                                     /* DENS: the count, in words */
        char b[16];
        fmt_int(b, n);
        str_cpy(b + str_len(b), " GRAINS", 8);
        cv_text_r(VX1, VY1 - 12, &AF_S, b, T_ACCENT, T_BG);
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
    int32_t x, py = VY1, root16 = v[0] * 16, width16 = 4 + (100 - v[1]) * 28 / 100, dry = VY1 - VH * 35 / 100;
    vz_caption("RESONANCE", 1);
    for (x = VX0; x <= VX1; x += 28)                                    /* octave grid */
        vz_vline(x, VY0, VY1, T_LINE);
    cv_rect(VX0, dry, VW, 1, vz_hi(f == 3u, T_DIM));                   /* the dry level */
    for (x = VX0; x <= VX1; x++) {
        int32_t n16 = 28 * 16 + (x - VX0) * 96 * 16 / VW, best = 0, k, y;
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
        y = VY1 - best * VH / 1000;
        if (x > VX0)
            cv_line_t(x - 1, py, x, y, vz_hi(f == 1u, T_THEME), 2);
        py = y;
    }
    {   /* ROOT: its peak, named */
        int32_t rx = VX0 + (root16 - 28 * 16) * VW / (96 * 16);
        char b[8];
        const char *u;
        param_format(&DEV_P[DEV_RESO][0], v[0], b, &u);
        vz_vline(rx, VY0, VY1, vz_hi(f == 0u, T_MID));
        cv_text_on(rx + 3, VY0, &AF_S, b, vz_hi(f == 0u, T_MID), T_BG);
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
    int32_t bx = VX0, bw = 64, wx = VX0 + 76, ww = VX1 - wx, i, py = 0, held = 0, hold = 1 + v[1] * 11 / 100;
    int32_t lp = 0, a = 3 * (100 - v[3]) / 4 + 8;                       /* TONE: the low-pass step, 8..83 / 100 */
    const char *what = f == 3u ? "TONE" : f == 2u ? "NOISE" : "DRIVE + CRUSH";
    vz_caption(what, 1);
    /* the inset: the transfer curve, or (TONE) the tone filter's response */
    cv_rrect(bx, VY0, bw, VH, 3, T_RAISE, T_BG);
    if (f == 3u) {
        for (i = 0; i < bw - 4; i++) {
            int32_t fr = i * 100 / (bw - 4), g = 100 * a / (a + fr + 1), y = VY1 - 4 - g * (VH - 8) / 100;
            cv_rect(bx + 2 + i, y, 1, 2, T_ACCENT);
        }
    } else {
        int32_t pyc = VY1;
        for (i = 0; i < bw - 4; i++) {
            int32_t x = (i * 2 - (bw - 4)) * 32767 / (bw - 4), y = color_shape(x, v);
            int32_t yy = VMID - y * (VH / 2 - 4) / 32767;
            if (i)
                cv_line(bx + 1 + i, pyc, bx + 2 + i, yy, vz_hi(f <= 1u && ui.last < 4u, T_THEME));
            pyc = yy;
        }
    }
    /* the waveform: two cycles of a sine through the device */
    cv_rect(wx, VMID, ww, 1, T_LINE);
    vz_seed = 777u;
    for (i = 0; i < ww; i++) {
        int32_t s = sine_i((uint32_t)i * (0xFFFFFFFFu / (uint32_t)ww) * 2u), y, nz, env;
        if (i % hold == 0)
            held = color_shape(s, v);                                  /* CRUSH's rate: hold a value */
        env = s < 0 ? -s : s;
        nz = ((int32_t)(vz_rand() & 0xFFFF) - 32768) * v[2] / 100;      /* NOISE, before TONE */
        lp += (nz - lp) * a / 100;
        y = held + (lp * env >> 16);                                   /* (|lp| <= 32768, env <= 32767: fits) */
        y = VMID - clamp(y, -32767, 32767) * (VH / 2 - 2) / 32767;
        if (i)
            cv_line(wx + i - 1, py, wx + i, y, f == 2u ? T_ACCENT : T_TEXT);
        py = y;
    }
}

/* ------------------------------------------------------------- SPACE --- */
/* the window is 2 s: the dry hit at 0, echoes TIME ms apart falling by FDBK, the tail rising over SIZE
 * (10..90 ms) and falling over DECAY (0.2..4.2 s) */
static void viz_space(const int16_t *v, uint32_t f)
{
    int32_t t, x, py = VY1, rise = 10 + v[2] * 80 / 100, len = 200 + v[3] * 40, g = 32767;
    vz_caption("ECHOES + TAIL", 1);
    for (x = VX0; x <= VX1; x++) {                                      /* the tail, filled */
        int32_t ms = (x - VX0) * 2000 / VW, e;
        if (ms < rise)
            e = ms * 1000 / rise;
        else
            e = 1000 - (ms - rise) * 1000 / len;
        if (e <= 0)
            continue;
        e = e * e / 1000 * 55 / 100;                                    /* exponential-ish, 55 % of the height */
        vz_vline(x, VY1, VY1 - e * VH / 1000, ux_mix(f >= 2u ? T_ACCENT : T_THEME, T_BG, 30));
        if (x > VX0)
            cv_line(x - 1, py, x, VY1 - e * VH / 1000, f >= 2u ? T_ACCENT : T_THEME);
        py = VY1 - e * VH / 1000;
    }
    cv_rect(VX0, VY0, 2, VH, T_TEXT);                                   /* the dry hit */
    for (t = v[0]; t < 2000 && g > 300; t += v[0]) {                    /* the echoes */
        g = (t == v[0]) ? 26000 : g * v[1] / 100;
        x = VX0 + t * VW / 2000;
        cv_rect(x, VY1 - g * VH / 32767, 2, g * VH / 32767, vz_hi(f <= 1u && ui.last < 4u, T_THEME));
    }
    cv_rect(VX0, VY1, VW, 1, T_LINE);
}

/* ------------------------------------------------------------- MODS --- */
static void viz_wave(const int16_t *v, uint16_t c, uint32_t f)
{
    /* SHAPE 0 sine, 1 triangle, 2 square; SKEW moves the peak (-100..100); FOLD folds the top back (0..100) */
    int32_t i, py = VMID, sk = clamp(500 + v[3] * 5, 50, 950);         /* the peak's place, 1/1000 of a cycle */
    vz_caption(v[1] == 0 ? "WAVE: SINE" : v[1] == 1 ? "WAVE: TRIANGLE" : "WAVE: SQUARE", 1);
    cv_rect(VX0, VMID, VW, 1, T_LINE);
    for (i = 0; i <= VW; i++) {
        int32_t p = (i * 2000 / VW) % 1000, q, y;                       /* two cycles */
        q = p < sk ? p * 500 / sk : 500 + (p - sk) * 500 / (1000 - sk); /* SKEW: a warped phase */
        if (v[1] == 0)
            y = sine_i((uint32_t)q * 4294967u);
        else if (v[1] == 1)
            y = q < 250 ? q * 131 : q < 750 ? (500 - q) * 131 : (q - 1000) * 131;
        else
            y = q < 500 ? 30000 : -30000;
        y = y * (100 + v[2] * 3) / 100;                                 /* FOLD: overdrive, then fold back */
        while (y > 32767 || y < -32767)
            y = y > 0 ? 65534 - y : -65534 - y;
        y = VMID - y * (VH / 2 - 2) / 32767;
        if (i)
            cv_line_t(VX0 + i - 1, py, VX0 + i, y, c, 2);
        py = y;
    }
}

static void viz_random(const int16_t *v, uint16_t c, uint32_t f)
{
    /* 16 values; SMOOTH glides between them; SPREAD scales them; BIAS shifts them */
    int32_t i, k, prev = 0, py = VMID, steps = 16, sw = VW / 16;
    int32_t val[16];
    vz_caption("RANDOM", 1);
    cv_rect(VX0, VMID, VW, 1, T_LINE);
    vz_seed = 4242u;
    for (k = 0; k < steps; k++)
        val[k] = clamp(((int32_t)(vz_rand() % 2001u) - 1000) * v[2] / 100 + v[3] * 10, -1000, 1000);
    for (i = 0; i <= VW; i++) {
        int32_t s = i / sw < steps ? i / sw : steps - 1, w = i - s * sw, sl = v[1] * sw / 100, y;
        int32_t cur = val[s];
        prev = s ? val[s - 1] : val[steps - 1];
        y = sl && w < sl ? prev + (cur - prev) * w / sl : cur;          /* SMOOTH: the glide */
        y = VMID - y * (VH / 2 - 2) / 1000;
        if (i)
            cv_line_t(VX0 + i - 1, py, VX0 + i, y, c, 2);
        py = y;
    }
}

static void viz_adsr(const int16_t *v, uint16_t c, uint32_t f)
{
    /* the times (0..127 on TIME_MS_X10: 1 ms .. 10 s) on a square-root scale so short and long both read */
    int32_t a = (int32_t)TIME_MS_X10[v[0] & 127], d = (int32_t)TIME_MS_X10[v[1] & 127], r = (int32_t)TIME_MS_X10[v[3] & 127];
    int32_t sa = 1, sd = 1, sr = 1, tot, xa, xd, xs, xr, sus = VY1 - v[2] * VH / 100;
    while (sa * sa < a) sa++;
    while (sd * sd < d) sd++;
    while (sr * sr < r) sr++;
    tot = sa + sd + sr + (sa + sd + sr) / 3;                            /* (a sustain plateau a third as long) */
    xa = VX0 + sa * VW / tot;
    xd = xa + sd * VW / tot;
    xs = xd + (sa + sd + sr) / 3 * VW / tot;
    xr = VX1;
    vz_caption("ADSR", 1);
    cv_rect(VX0, VY1, VW, 1, T_LINE);
    cv_line_t(VX0, VY1, xa, VY0, c, 2);
    cv_line_t(xa, VY0, xd, sus, c, 2);
    cv_line_t(xd, sus, xs, sus, c, 2);
    cv_line_t(xs, sus, xr, VY1, c, 2);
    if (f < 4u) {                                                       /* the last-turned stage: a marker under it */
        int32_t m0 = f == 0u ? VX0 : f == 1u ? xa : f == 2u ? xd : xs, m1 = f == 0u ? xa : f == 1u ? xd : f == 2u ? xs : xr;
        cv_rect(m0, VY1 + 2, m1 - m0, 2, c);
    }
}

static void viz_seq(const int16_t *v, uint16_t c, uint32_t f)
{
    /* the 16 steps (their values arrive with p-locks, phase 7: at 0 for now), STEPS active, SWING's offbeats,
     * SLEW's ramps */
    int32_t k, sw = VW / 16;
    vz_caption("SEQUENCE", 1);
    cv_rect(VX0, VMID, VW, 1, T_LINE);
    for (k = 0; k < 16; k++) {
        int32_t x = VX0 + k * sw + ((k & 1) ? v[3] * sw / 300 : 0), on = k < v[0];
        cv_rrect(x + 2, VMID - 2, sw - 4 - v[2] * (sw - 6) / 100, 5, 2, on ? c : T_RAISE, T_BG);
        if (on && v[2])
            cv_line(x + sw - 2 - v[2] * (sw - 6) / 100, VMID, x + sw + 2, VMID, c);
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
    vz_caption(ui.glo_held ? "HOLD GLO + KEY 1-4: TRACK" : "TRACKS", 0);
    for (t = 0; t < NTRK; t++) {
        int32_t x = 8 + 58 * (int32_t)t, sel = t == sys.sel, lv = track[t].level * 50 / 127;
        int32_t m = meter_w(track_rt[t].peak, 50);
        char n[3] = {'T', (char)('1' + t), 0};
        uint16_t bg = sel ? T_SURF : T_BG;
        cv_rrect(x, VY0, 52, VH + 2, 5, bg, T_BG);
        if (sel)
            cv_rect(x + 8, VY0 + VH, 36, 2, T_ACCENT);         /* the focused track: a bar at the foot, not colour alone */
        cv_text_on(x + 6, VY0 + 4, &AF_M, n, sel ? T_TEXT : T_MID, bg);
        cv_rrect(x + 40, VY1 - 44, 5, 44, 2, T_RAISE, bg);     /* the level, right of the words */
        cv_rrect(x + 40, VY1 - lv * 44 / 50, 5, lv * 44 / 50 < 3 ? 3 : lv * 44 / 50, 2, T_THEME, T_RAISE);
        cv_rrect(x + 47, VY1 - 44, 3, 44, 1, T_RAISE, bg);     /* the meter */
        if (m)
            cv_rrect(x + 47, VY1 - m * 44 / 50, 3, m * 44 / 50 < 3 ? 3 : m * 44 / 50, 1, track[t].mute ? T_DIM : T_TEXT, T_RAISE);
        if (track[t].mute)
            cv_text_on(x + 4, VY0 + 26, &AF_S, "MUTE", T_MID, bg);   /* under the number, clear of the bars */
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

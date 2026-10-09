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
 *   DRUM       PATTERN, VARY: the bar as a grid of what this pass plays (written, left out, added); KIT: the
 *              instrument's sound; STEP: the instrument's row of the bar
 *   TAPE       the whole tape's sound, large, lit inside the loop window (STRT, LEN, bracketed) with its 16 slices
 *              ticked under it; the playhead over it (stopped: where playing starts, moved by ROTA)
 *   GRAIN      the grains as they sound: where each reads (across), its pitch (up), its stretch of the sound
 *              under its window with the playhead running its way; the cursor and SPRY's reach; FROZEN
 *   RESONATOR  the response over 8 octaves: peaks on PTCH's partials (harmonics, or a scale's chord tones; a node on
 *              each, the root's filled and named), as sharp as DEC makes them, kept up top by TONE, shaped by the
 *              filter before them (dotted: CUT RES SLOP), blended with the dotted dry line by WET
 *   COLOR      a sine through the device (dotted: in; solid: out); the inset follows the last-turned knob: the
 *              transfer curve (DRIV, CRSH, CMOD), the noise after a hit (NOIS, NDEC), its filter (NTON), TILT
 *   SPACE      the dry hit, the echoes as stems with nodes (TIME apart, falling by FDBK) and the reverb tail
 *   MOD slots  LFO's shape (RND: its loop of steps) and its next time round dotted, ADSR's envelope with its
 *              stages named and bent, SEQ's 16 steps as bars of their values, FOLLOW's envelope over what it listens to
 *   REC IN     (ALGORITHM) who records whom: the tracks as boxes, a line from each one heard into the recorder
 *   MIXER      the levels: what each track holds of the shared memory, TRACKS and the time free; EDIT (the
 *              channel): the selected track's EQ and filter as one response, the pan as two speakers
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
static void viz_tape(const int16_t *v, uint32_t f)
{
    /* The whole of what the track plays (its tape or a reel, named at the top left), its sound drawn large from the
     * blocks' peaks (each dot column the loudest block under it, so no hit falls between columns): lit inside the
     * loop window (STRT, LEN, bracketed), dim outside, with the 16 slices the white keys play ticked under it. The
     * playhead is where the head is while it runs (a dotted cut through the sound), else where playing starts
     * (ROTA's point; the end when reversed); REC armed tagged at the top right. SPD's direction and speed are the
     * strip's pictogram above, so the sound gets the rows they took here. */
    int32_t r0 = 6, r1 = 114, rw = r1 - r0, top = 10, bot = 35, mid = 23, amp = 12, k, x;
    int32_t x0 = r0 + v[0] * rw / 100, x1 = x0 + v[1] * rw / 100;
    int32_t sp = v[2] * (v[5] ? -1 : 1) / (v[6] ? 2 : 1), gain = db_x1000(v[7]) * db_x1000(v[TK_LVL]) / 1000;   /* REV,
                                                                                * HALF; GAIN, LVL */
    int32_t fw = 1 + v[4] * 8 / 100;                   /* XFAD (drawn wider than to scale, so it shows) */
    tape_view_t tv;
    uint32_t nb;
    if (x1 > r1)
        x1 = r1;
    tape_view(sys.sel, &tv);
    nb = tv.len / TAPE_BLK;
    px_line(r0, mid, r1, mid, px_dim, 2);                            /* the centre line */
    for (x = r0; x <= r1 && nb; x++) {                                 /* the sample: each column's loudest block */
        uint32_t b0 = (uint32_t)(x - r0) * nb / (uint32_t)(rw + 1), b1 = (uint32_t)(x - r0 + 1) * nb / (uint32_t)(rw + 1), b, pk = 0;
        int32_t a;
        for (b = b0; b <= b1 && b < nb; b++)
            pk = tv_peak(&tv, b) > pk ? tv_peak(&tv, b) : pk;
        a = clamp((int32_t)pk * amp * gain / (255 * 1000), 0, amp);   /* (GAIN: clips at the frame) */
        if (a)
            px_box(x, mid - a, 1, 2 * a + 1, x >= x0 && x <= x1 ? px_ink : px_dim);
    }
    for (k = 1; k < 16; k++)                                           /* the slices, ticked under the window */
        px_dot(x0 + (x1 - x0) * k / 16, bot + 1, px_dim);
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
    {   /* the playhead: a triangle over the sound, where the head is (or where playing starts) */
        int32_t xr, hd = tape_head(sys.sel), px;
        xr = x0 + (x1 - x0) * v[9] / 100;              /* ROTATE: where playing starts in the window */
        px = hd >= 0 ? r0 + hd * rw / 1000 : sp < 0 ? (v[9] ? xr - 1 : x1 - 3) : (v[9] ? xr : x0 + 3);
        for (k = 0; k < 3; k++)
            px_box(px - 2 + k, top - 5 + k, 5 - 2 * k, 1, px_ink);
        if (hd >= 0) {                                 /* running: a cut through the sound, and its edges */
            px_box(px - 1, top + 1, 3, bot - top - 1, px_bg);
            px_line(px, top + 1, px, bot - 1, px_ink, 2);
        }
    }
    px_text(1, 0, PXF_3, tape_name(sys.sel), px_ink);                /* what it plays */
    if ((sys.rec >> sys.sel) & 1u)
        px_tag(105, 0, PXF_3, "REC", px_ink, px_bg);
    vz_label(x0, "IN", f == 0u);
    if (x1 - x0 >= 22 || f == 1u)
        vz_label(x1, "OUT", f == 1u);
    if (f == 3u || f == 2u)
        vz_label(60, f == 3u ? "DUB" : "SPD", 1);   /* (SPD's turn still names it: the playhead's side shows REV) */
    if (f >= 4u && f < NPK && !pdesc_empty(&DEV_P[DEV_SRC][f]))
        vz_ktag(78, DLBL - 1, &DEV_P[DEV_SRC][f], v[f]);
}

/* a source's LEVEL page (SYNTH 5, POLY 4): the dB scale from -24 to +6, the level as a bar along it, 0 dB marked */
static void vz_level(int32_t db, const pdesc_t *d, int on)
{
    int32_t x, k, y0 = DMID - 5, y1 = DMID + 5, xl = DX0 + (clamp(db, -24, 6) + 24) * DW / 30, xz = DX0 + 24 * DW / 30;
    px_frame(DX0, y0, DW + 1, y1 - y0 + 1, px_dim, 2);
    px_box(DX0 + 1, y0 + 2, xl - DX0, y1 - y0 - 3, px_ink);
    for (x = xz, k = y0 - 4; k <= y1 + 3; k += 2)                     /* 0 dB: where the source is as it plays */
        px_dot(x, k, px_ink);
    for (k = -24; k <= 6; k += 6) {                                   /* the scale */
        char b[6];
        x = DX0 + (k + 24) * DW / 30;
        px_line(x, y1 + 2, x, y1 + 3, px_dim, 1);
        if (k > 0) {
            b[0] = '+';
            fmt_int(b + 1, k);
        } else {
            fmt_int(b, k);
        }
        px_text(clamp(x - px_text_w(PXF_3, b) / 2, 1, 118 - px_text_w(PXF_3, b)), DLBL, PXF_3, b, px_dim);
    }
    px_text(DX0, DY0, PXF_3, "LEVEL", px_ink);
    if (on)
        vz_ktag(118, DY0 - 1, d, db);
}

/* ------------------------------------------------------------- GRAIN --- */
/* d, a distance along a loop of ll samples, the short way round */
static int32_t gr_wrap(int32_t d, int32_t ll)
{
    while (d > ll / 2)
        d -= ll;
    while (d < -ll / 2)
        d += ll;
    return d;
}

static void viz_grain(const int16_t *v, uint32_t f)
{
    /* A field of the grains as they sound, not the whole sample. Across: where a grain reads; up and down: its pitch
     * (PTCH and its PRND, the sound's own in the middle, +-2 octaves at the edges). A grain is drawn while it sounds:
     * the stretch it reads (wider when it reads faster, narrower slowed down), its height its window (CONT) swelled
     * by the level there, played solid and still to play dim, its playhead an arrow the way it runs (REV: right to
     * left). Nothing sounding (stopped, or WET 0): one grain's outline at the cursor, from the knobs.
     *   SCAN TAPE: the stretch of TAPE's loop window around the cursor (in the middle) that grains can reach, SPRY's
     *              range dotted under it.
     *   the buffer (STR POS DLY): all of it, its bars (the bar lines dotted, the beats ticked), the write head a
     *              solid line while it records (gone while frozen), the cursor the triangle on top, the sound's
     *              level along the bottom. WET 0: the bars it will hold, empty. */
    grain_trk_t *G = &grain[sys.sel];
    gr_buf_t *B = &gbuf[sys.sel];
    tape_view_t tv;
    uint32_t scan = (uint32_t)clamp(v[GP_SCAN], 0, 3), buf = scan != SCAN_TAPE;
    int32_t ls, ll, i, k, x, cx, yc0 = buf ? 16 : 19, n = 0, sp, reach, gspan, fw, cur;
    char b[16];
    if (buf) {
        gr_buf_view(sys.sel, &tv);
        ls = 0;
        ll = (int32_t)tv.len;
        if (!ll && v[GP_WET]) {                                        /* on, but nothing to hold it in */
            px_text_c(0, 120, 12, PXF_3, "NO MEMORY FREE", px_dim);
            px_text_c(0, 120, 20, PXF_3, "FOR THE BUFFER", px_dim);
            goto labels;
        }
        if (!ll)                                                       /* WET 0: the bars it will hold, empty */
            ll = (int32_t)(gr_bars() * (5292000u / (sys.bpm ? sys.bpm : 120u)));
    } else {
        tape_view(sys.sel, &tv);
        if (!tv.len) {
            px_text_c(0, 120, 16, PXF_3, "NOTHING ON THE TAPE", px_dim);
            return;
        }
        tape_window(sys.sel, tv.len, &ls, &ll);
    }
    cur = G->cur >> 12;
    reach = ll * v[GP_SPRY] / 200;                                     /* SPRY: samples either way */
    gspan = (int32_t)((uint32_t)gr_rate(v[GP_PTCH] * 16) * ((uint32_t)v[GP_SIZE] * 441u / 10u) >> 12);   /* a grain, read */
    if (buf) {                                                         /* the whole buffer, left to right */
        fw = ll;
        cx = DX0 + (int32_t)((uint32_t)(cur - ls) * (uint32_t)DW / (uint32_t)ll);
    } else {                                                           /* the cursor's neighbourhood, centred */
        fw = clamp(2 * reach + 3 * gspan > 4 * gspan ? 2 * reach + 3 * gspan : 4 * gspan, 256, ll);
        cx = 60;
    }
#define GX(s) (buf ? DX0 + (int32_t)((uint32_t)((((s) - ls) % ll + ll) % ll) * (uint32_t)DW / (uint32_t)ll) \
                   : cx + gr_wrap((s) - cur, ll) * DW / fw)            /* a sample's column */
#define GY(st16) (yc0 - (st16) * (buf ? 11 : 14) / (24 * 16))        /* a pitch's row */
    if (buf) {                                                         /* the bars and beats, the write head, the level */
        uint32_t bars = tv.len && B->bars ? B->bars : gr_bars(), q;
        for (q = 0; q <= bars * 4u; q++) {
            int32_t xq = DX0 + (int32_t)(q * (uint32_t)DW / (bars * 4u));
            if (q % 4u == 0u)
                px_line(xq, 4, xq, 28, px_dim, 2);
            else
                px_dot(xq, 4, px_dim);
        }
        for (x = DX0; x <= DX1; x++) {                                 /* the level along the bottom: 3 dots at most */
            uint32_t s0 = (uint32_t)(x - DX0) * (uint32_t)ll / (uint32_t)DW, bb = s0 / TAPE_BLK;
            int32_t a = bb < tv.len / TAPE_BLK ? (int32_t)tv_peak(&tv, bb) * 4 / 256 : 0;
            if (a)
                px_box(x, 33 - a, 1, a, px_dim);
        }
        if (!grain_frozen && tv.len && (sys.playing || tp[sys.sel].src != SRC_TAPE)) {   /* (only while it records) */
            int32_t xw = GX(B->w);
            px_line(xw, 2, xw, 33, px_ink, 1);
        }
    } else {
        sp = reach * DW / fw;                                          /* SPRY's reach, dots */
        px_line(clamp(cx - sp, DX0, DX1), 36, clamp(cx + sp, DX0, DX1), 36, px_dim, 2);   /* where grains may start */
    }
    px_line(DX0, yc0, DX1, yc0, px_dim, 4);                            /* the sound's own pitch */
    for (k = 0; k < (int32_t)GR_SLOTS; k++) {                          /* the grains sounding */
        const grain_t *g = &gslot[k];
        int32_t rate, span, p0, a0, xa, xb, ph, yc, fr;
        if (!((gr_used >> k) & 1u) || g->trk != sys.sel || !g->len)
            continue;
        n++;
        rate = g->inc < 0 ? -g->inc : g->inc;
        span = (int32_t)((uint32_t)rate * g->len >> 12);
        p0 = (g->pos >> 12) - ((g->inc * (int32_t)(g->len - g->left)) >> 12);   /* (fits: |inc| < 2^15, len < 2^15) */
        a0 = g->rev ? p0 - span : p0;
        xa = GX(a0);
        xb = xa + span * DW / fw;
        if (xb <= xa)
            xb = xa + 1;
        ph = xa + ((g->pos >> 12) - a0) * DW / fw;
        yc = clamp(GY(g->st16), 5, buf ? 28 : 33);
        fr = (int32_t)(g->fade * 1000u / g->len);                     /* each ramp, of the grain (1/1000) */
        for (x = clamp(xa, DX0, DX1); x <= clamp(xb, DX0, DX1); x++) {
            int32_t at = (x - xa) * 1000 / (xb - xa), e = 32767, s = a0 + (x - xa) * span / (xb - xa), h;
            uint32_t blk = (uint32_t)clamp(s, 0, (int32_t)tv.len - 1) / TAPE_BLK;
            int on = g->rev ? x >= ph : x <= ph;                       /* played */
            if (at < fr)
                e = GR_RAMP[at * 128 / (fr ? fr : 1)];
            else if (1000 - at < fr)
                e = GR_RAMP[(1000 - at) * 128 / (fr ? fr : 1)];
            h = (2 + clamp((int32_t)tv_peak(&tv, blk) * 3, 0, 255) * (buf ? 3 : 5) / 255) * e / 32767;   /* (quiet stretches: still a shape) */
            px_box(x, yc - h, 1, 2 * h + 1, on ? px_ink : px_dim);
        }
        if (ph >= DX0 && ph <= DX1) {                                  /* the playhead, an arrow its way */
            int32_t d = g->rev ? -1 : 1;
            px_box(ph, yc - 3, 1, 7, px_ink);
            px_dot(ph + d, yc - 1, px_ink);
            px_dot(ph + d, yc + 1, px_ink);
            px_dot(ph + 2 * d, yc, px_ink);
        }
    }
    if (!n) {                                                          /* nothing sounding: a grain from the knobs */
        int32_t span = gspan, xa = cx, xb = cx + span * DW / fw, yc = clamp(GY(v[GP_PTCH] * 16), 5, buf ? 28 : 33);
        int32_t fr = v[GP_CONT] * 5, pyu = yc, pyd = yc, hm = buf ? 4 : 6;
        if (xb <= xa + 2)
            xb = xa + 3;
        for (x = xa; x <= xb && x <= DX1; x++) {
            int32_t at = (x - xa) * 1000 / (xb - xa), e = 32767, h;
            if (at < fr)
                e = GR_RAMP[at * 128 / (fr ? fr : 1)];
            else if (1000 - at < fr)
                e = GR_RAMP[(1000 - at) * 128 / (fr ? fr : 1)];
            h = hm * e / 32767;
            if (x > xa) {
                px_line(x - 1, pyu, x, yc - h, px_dim, 1);
                px_line(x - 1, pyd, x, yc + h, px_dim, 1);
            }
            pyu = yc - h;
            pyd = yc + h;
        }
        if (v[GP_REV]) {                                               /* REV: some run backwards */
            px_dot(xa - 1, yc - 1, px_dim);
            px_dot(xa - 1, yc + 1, px_dim);
            px_dot(xa - 2, yc, px_dim);
        }
    }
    for (i = 0; i < 3; i++)                                            /* the cursor, where grains start */
        if (cx >= DX0 && cx <= DX1)
            px_box(cx - 2 + i, i, 5 - 2 * i, 1, px_ink);
    if (grain_frozen)
        px_tag(118 - px_text_w(PXF_3, "FROZEN") - 1, 0, PXF_3, "FROZEN", px_ink, px_bg);
#undef GX
#undef GY
labels:
    if (!v[GP_WET])
        str_cpy(b, "WET 0: OFF", sizeof b);
    else {
        fmt_int(b, n);                                                 /* "5 OF 12 GRAINS": its share now */
        str_cpy(b + str_len(b), " OF ", 5);
        {
            uint8_t sh[NTRK];                                          /* (worked out here as the ISR does: right */
            gr_plan_into(sh);                                          /*  before the first block has run too) */
            fmt_int(b + str_len(b), sh[sys.sel]);
        }
        str_cpy(b + str_len(b), " GRAINS", 8);
    }
    if (f == 1u || f == 4u)
        px_tag(1, DLBL - 1, PXF_3, b, px_ink, px_bg);
    else
        px_text(2, DLBL, PXF_3, b, px_ink);
    if (f >= 5u && f < 16u && !pdesc_empty(&DEV_P[DEV_GRAIN][f]))
        vz_ktag(118, DLBL - 1, &DEV_P[DEV_GRAIN][f], v[f]);
    else if (buf) {                                                    /* the buffer: its bars ("STR 2 BARS") */
        char m[16];
        str_cpy(m, N_SCAN[scan], sizeof m);
        str_cpy(m + str_len(m), " ", 2);
        uint32_t bars = tv.len && B->bars ? B->bars : gr_bars();
        fmt_int(m + str_len(m), (int32_t)bars);
        str_cpy(m + str_len(m), bars > 1u ? " BARS" : " BAR", 6);
        px_text(118 - px_text_w(PXF_3, m), DLBL, PXF_3, m, px_dim);
    } else
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
 * shapes what reaches them; WET blends with the flat dry line (35 %), or (ROUT SEND) adds to it; 0..1000 */
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
    if (v[RP_ROUT])
        return clamp(350 + best * v[3] / 100 * 65 / 100, 0, 1000);
    return (best * v[3] + 350 * (100 - v[3])) / 100;
}

static void viz_reso(const int16_t *v, uint32_t f)
{
    static const char *const NAME[9] = {"PTCH", "DEC", "TONE", "WET", "CUT", "RES", "SLOP", "SCAL", "ROUT"};
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
    vz_label((DX0 + DX1) / 2, f == RP_ROUT ? N_ROUT[clamp(v[RP_ROUT], 0, 1)] : f >= 1u && f < 9u ? NAME[f] : N_RSCAL[sc],
             f >= 1u && f < 9u);
    if (f >= 4u && f < 8u)
        vz_ktag(118, DY0 - 1, &DEV_P[DEV_RESO][f], v[f]);
}

/* ------------------------------------------------------------- COLOR --- */
/* the mappings color.c plays (color_shape there: the drive and the bits): DRIV 0..100 -> gain 1..16 into the soft
 * clip (0: none); CRSH 0..100 takes, by CMOD, 16..2 bits (BIT), a hold of 1..12 samples (RATE), or both; NOIS:
 * noise riding the signal's envelope, which falls over NDEC after the sound; NTON: a one-pole filter on that noise
 * (- dark .. + bright); TILT: the low end against the high (on a sine: nothing to draw, so the inset shows it); WET:
 * the dry and the coloured signal; LVL: the output in dB */

static int32_t color_out(int32_t x, int32_t shaped, const int16_t *v)   /* WET (INS: a crossfade; SEND: added to the
                                                                           * dry), then LVL */
{
    if (v[CP_ROUT])
        return (x + shaped * v[7] / 100) * db_x1000(v[8]) / 1000;
    return (x + (shaped - x) * v[7] / 100) * db_x1000(v[8]) / 1000;
}

static void viz_color(const int16_t *v, uint32_t f)
{
    static const char *const NAME[10] = {"DRIV", "CRSH", "NOIS", "TILT", "NDEC", "NTON", "CMOD", "WET", "LVL", "ROUT"};
    int32_t bx = DX0, bw = 32, wx = DX0 + 37, ww = DX1 - wx, i, py = 0, held = 0;
    int32_t hold = (int32_t)col_hold(v);                              /* CMOD RATE or BOTH: hold a value */
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
    vz_label(bx + bw / 2, f == CP_ROUT ? N_ROUT[clamp(v[CP_ROUT], 0, 1)] : f < 10u ? NAME[f] : "CURVE", f < 10u);
    vz_label(wx + ww / 2, "IN : OUT", 0);
    if (f >= 4u && f < 9u)
        vz_ktag(118, DY0 - 1, &DEV_P[DEV_COLOR][f], v[f]);
}

/* ------------------------------------------------------------- SPACE --- */
/* the window is 2 s: the dry hit at 0, echoes TIME ms apart falling by FDBK, the tail rising over SIZE (10..90 ms)
 * and falling over DECAY (0.2..4.2 s). DLY or VERB at 0 is off (space.c: no memory taken): its part is drawn dim,
 * as it would be at 100, so TIME, FDBK, SIZE and DEC still show what they set */
static void viz_space(const int16_t *v, uint32_t f)
{
    /* page 2: DLY scales the echoes, VERB the tail; TONE (- low-pass, + high-pass on the feedback) makes each echo
     * lose a little more; page 3: PRE holds the tail back (ms) */
    int32_t t, x, py = DY1, rise = 10 + v[2] * 80 / 100, len = 200 + v[3] * 40, g = 32767, first = -1, pre = v[8];
    int32_t tl = 100 - (v[6] < 0 ? -v[6] : v[6]) / 5;                  /* TONE: what each pass keeps, % */
    int32_t dly = v[4] ? v[4] : 100, verb = v[5] ? v[5] : 100;
    uint16_t dink = v[4] ? px_ink : px_dim, vink = v[5] ? px_ink : px_dim;
    px_line(DX0, DY1 + 1, DX1, DY1 + 1, px_dim, 2);
    for (x = DX0; x <= DX1; x++) {                                     /* the tail: its edge alone, no fill */
        int32_t ms = (x - DX0) * 2000 / DW - pre, e, y;
        if (ms < 0) {
            py = DY1;
            continue;
        }
        e = ms < rise ? ms * 1000 / rise : 1000 - (ms - rise) * 1000 / len;
        if (e <= 0)
            break;
        e = e * e / 1000 * verb / 100;
        y = DY1 - e * DH / 1000;
        if (x > DX0)
            px_line(x - 1, py, x, y, vink, 1);
        py = y;
    }
    px_line(DX0 + 1, DY1, DX0 + 1, DY0 + 1, px_ink, 1);                /* the dry hit */
    vz_node(DX0 + 1, DY0 + 1, 1);
    for (t = v[0]; t < 2000 && g > 1500; t += v[0]) {                  /* the echoes: stems with nodes */
        int32_t h;
        g = (t == v[0]) ? 26000 : g * v[1] / 100 * tl / 100;
        x = DX0 + t * DW / 2000;
        h = g * DH / 32767 * dly / 100;
        if (first < 0)
            first = x;
        px_line(x, DY1, x, DY1 - h, dink, 1);
        if (v[4])
            vz_node(x, DY1 - h, f <= 1u || f == 4u);
        else
            px_dot(x, DY1 - h, px_dim);
    }
    if (f >= 4u && f < 9u)
        vz_ktag(118, DY0 - 1, &DEV_P[DEV_SPACE][f], v[f]);
    px_text(DX0, DLBL, PXF_3, "DRY", px_ink);
    if (first >= DX0 + 20 && first < DX1 - 20)
        vz_label(first + 2, f == 1u ? "FDBK" : "ECHO", f <= 1u || f == 4u);
    vz_label(DX1 - 10, f == 2u ? "SIZE" : f == 3u ? "DEC" : "TAIL", (f >= 2u && f <= 3u) || f == 5u || f == 8u);
}

/* ------------------------------------------------------------- MODS --- */
/* The modulators share the S-4's placement knobs: AMT scales the shape, OFS moves it up or down, PHAS starts it
 * later in its cycle, SPRD is the right channel's phase against the left (drawn as a dotted second trace). A
 * page 2 or 3 knob you turn is tagged with its value at the right of the label row. */
static int32_t vz_place(int32_t y, int32_t amt, int32_t ofs)         /* Q15 -> Q15: AMT, then OFS */
{
    return clamp(y * amt / 100 + ofs * 327, -32767, 32767);
}

/* The LFO's random steps: a loop of LEN values (Q15), and the next time round, drifted by VAR. One table for
 * both RND's steps and, on the other shapes, each cycle's level (VAR's drift repeats every LEN cycles). */
static void lfo_rand(const int16_t *v, int16_t *now, int16_t *next)
{
    int32_t k;
    vz_seed = 4242u;
    for (k = 0; k < 16; k++)
        now[k] = (int16_t)(((int32_t)(vz_rand() % 2001u) - 1000) * 32);
    for (k = 0; k < 16; k++)
        next[k] = (int16_t)clamp(now[k] + ((int32_t)(vz_rand() % 2001u) - 1000) * 32 * v[6] / 100, -32767, 32767);
}

/* (lfo_at and isqrt: mod.c, shared with the sound) */

static void viz_wave(const int16_t *v, uint32_t f)
{
    /* Two cycles from PHAS. SMTH slews what's drawn (RND's steps glide); VAR: the next time round, dotted (RND: the
     * loop's steps drifted; the other shapes: each cycle's level drifted, repeating every LEN cycles). FADE fades
     * the first cycle in; SYNC BPM puts the beat ticks on the line; TRIG KEY marks the key that restarts it; SPRD's
     * right channel, dim. AMT and OFS place it all. */
    static const char *const NAME[5] = {"SINE", "TRIANGLE", "SQUARE", "SAW", "RANDOM"};
    int32_t i, py = DMID, sh = clamp(v[1], 0, 4), fade = v[14] * DW / 200, half = DH / 2 - 1;
    int16_t now[16], nxt[16];
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
            lv = ys[k] + (ys[k + 1] - ys[k]) * env_bend(t, ys[k + 1] > ys[k] ? -c : c) / 1000;
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
    } else if (ui.page >= 4u) {
        vz_level(v[SY_LVL], &SYN_P[SY_LVL], f == SY_LVL);
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
            int32_t a = vw.len ? (int32_t)tv_peak(&vw, blk) * 9 / 255 : 0;
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
    } else if (ui.page >= 3u) {
        vz_level(v[PL_LVL], &POL_P[PL_LVL], f == PL_LVL);
    } else {
        vz_filter(v[PL_CUT], v[PL_RES], clamp(v[PL_TYPE], 0, 2), v[PL_ENV], 0, f < NPK ? f % 4u : 0xFFu, POL_P + 8,
                  v + 8, 3u);
        px_text(DX0, DY0, PXF_3, N_SLOP[clamp(v[PL_TYPE], 0, 2)], px_ink);
    }
}

/* -------------------------------------------------------------- DRUM --- */
static const char *const DRM_LONG[DRM_NINST] = {"BASS DRUM", "SNARE", "CLAP", "RIM SHOT", "LOW CONGA", "LOW BONGO",
                                                "HIGH BONGO", "CLAVES", "COWBELL", "MARACAS", "TAMBOURINE", "GUIRO",
                                                "HI-HAT", "OPEN HAT", "METAL BEAT", "CYMBAL"};

/* the bar a DRUM page shows: the one playing, else the one STEP shows */
static uint32_t vz_drm_bar(const int16_t *v)
{
    uint32_t len = (uint32_t)clamp(v[DM_LEN], 2, (int32_t)DRM_NBAR), now = drm_now[sys.sel];
    return sys.playing && now != 0xFFu ? (now >> 4) % len : (uint32_t)clamp(v[DM_BAR] - 1, 0, (int32_t)len - 1);
}

/* PATTERN, VARY, STEP in LIVE or ERAS: the bar as a grid, a row per instrument the pattern uses (up to five, left
 * to right on the keys; spaced out when there are fewer), its code at the left, the 16 steps in groups of four. What this pass plays: a hit as written
 * solid; one this version leaves out, a dot; one it adds, hollow (a quiet one: small). The accented steps marked over
 * the grid, the playhead under it, the bars at the right (the shown one filled). */
static uint32_t vz_drm_grid(const int16_t *v, uint32_t bar)
{
    uint32_t len = (uint32_t)clamp(v[DM_LEN], 2, (int32_t)DRM_NBAR), used = 0, now = drm_now[sys.sel], b, s, i, row = 0;
    uint32_t ver = drm_version(v, sys.playing ? drm_pass[sys.sel] : 0u), more = 0, nrow = 0;
    int32_t pitch;
    drm_hits_t h[16];
    for (b = 0; b < len; b++)
        for (s = 0; s < 16u; s++)
            used |= tp[sys.sel].dpat[b][s];
    for (s = 0; s < 16u; s++)
        drm_step(sys.sel, v, ver, bar, s, &h[s]);
    for (s = 0; s < 16u; s++)                                         /* (a version can bring in its crash) */
        used |= h[s].hit;
    for (i = 0; i < DRM_NINST; i++)
        nrow += (used >> i) & 1u;
    pitch = nrow <= 3u ? 9 : nrow == 4u ? 7 : 6;
    for (s = 0; s < 16u; s++) {                                       /* the accents, over the steps */
        int32_t x = 12 + (int32_t)s * 6 + (int32_t)(s / 4u);
        if (h[s].acc)
            px_box(x, DY0, 4, 2, px_ink);
        if (sys.playing && now != 0xFFu && now == bar * 16u + s)      /* the playhead, under them */
            px_box(x - 1, DY1 + 1, 6, 2, px_ink);
    }
    for (i = 0; i < DRM_NINST; i++) {
        int32_t y = DY0 + 4 + (int32_t)row * pitch;
        if (!((used >> i) & 1u))
            continue;
        if (row == 5u) {
            more++;
            continue;
        }
        px_text(DX0, y, PXF_3, DRM_KIT[i].code, px_ink);
        for (s = 0; s < 16u; s++) {
            int32_t x = 12 + (int32_t)s * 6 + (int32_t)(s / 4u);
            uint32_t w = (tp[sys.sel].dpat[bar][s] >> i) & 1u, p = (h[s].hit >> i) & 1u;
            if (p && (h[s].soft >> i) & 1u)
                px_frame(x + 1, y + 1, 2, 2, px_ink, 1);
            else if (p && w)
                px_box(x, y, 4, 4, px_ink);
            else if (p)
                px_frame(x, y, 4, 4, px_ink, 1);
            else if (w)
                px_dot(x + 1, y + 1, px_ink);
            else
                px_dot(x + 1, y + 2, px_dim);
        }
        row++;
    }
    if (!used)
        px_text_c(12, 100, DY0 + 14, PXF_3, "EMPTY: WRITE IT ON STEP", px_dim);
    for (b = 0; b < len; b++)                                         /* the bars */
        if (b == bar)
            px_box(113, DY0 + 4 + (int32_t)b * 8, 4, 6, px_ink);
        else
            px_frame(113, DY0 + 4 + (int32_t)b * 8, 4, 6, px_dim, 1);
    return more;                                                      /* (instruments past five: how many) */
}

/* KIT, or an instrument held: its sound, struck as the pattern would strike it (rendered off line through its
 * voice: what you see is what plays), the peaks over up to 0.74 s, its name */
static void vz_drm_sound(const int16_t *v, uint32_t i)
{
    static dk_coef_t c;
    static dk_voice_t vo;
    uint8_t pk[128];
    const int16_t *in = tp[sys.sel].dins[i];
    dk_knobs_t k;
    int32_t y[CTL], mid = 15, x;
    uint32_t nch = 0, j, m;
    k.tune = (int16_t)(clamp(v[DM_TUNE] + in[DIN_TUNE], -24, 24) * 16);
    k.dscale = (uint16_t)drm_scale(v[DM_DECY] + in[DIN_DECY]);
    k.bright = (int16_t)clamp((v[DM_TONE] + in[DIN_TONE]) * 127 / 100, -127, 127);
    k.level = (uint16_t)(db_q10(clamp(in[DIN_LVL], -24, 6)) * 100 / 1024);
    k.accent = 0;
    k.drive = (uint8_t)(clamp(v[DM_DRV], 0, 100) * 127 / 100);
    dk_setup(&c, &DRM_KIT[i], &k);
    memset(&vo, 0, sizeof vo);
    dk_trigger(&vo);
    while (nch < 128u && (vo.live || vo.trig)) {                      /* 256 samples a chunk */
        int32_t a = 0;
        for (j = 0; j < 8u; j++) {
            dk_run(&c, &vo, y, CTL);
            for (m = 0; m < CTL; m++)
                a = y[m] > a ? y[m] : -y[m] > a ? -y[m] : a;
        }
        pk[nch++] = (uint8_t)clamp(a * 16 / 16384, 0, 16);
    }
    px_line(DX0, mid, DX1, mid, px_dim, 2);
    for (x = DX0; x <= DX1; x++) {
        uint32_t ch = (uint32_t)(x - DX0) * 128u / (uint32_t)(DW + 1);
        int32_t a = ch < nch ? pk[ch] : 0;
        if (a)
            px_box(x, mid - a, 1, 2 * a + 1, px_ink);
    }
    {
        char b[24];
        str_cpy(b, DRM_KIT[i].code, 3);
        str_cpy(b + 2, " ", 2);
        str_cpy(b + 3, DRM_LONG[i], 16);
        px_text(DX0, DLBL, PXF_3, b, px_ink);
        px_text(DX1 - px_text_w(PXF_3, "0.74S") + 1, DY0, PXF_3, "0.74S", px_dim);
    }
}

/* DRUM's pages: PATTERN and VARY the bar's grid, KIT the instrument's sound (STEP's, or the one held), STEP the
 * instrument's row of the bar large (LIVE, ERAS: the grid) */
static void viz_drum(const int16_t *v, uint32_t f)
{
    uint32_t bar = vz_drm_bar(v), i = (uint32_t)clamp(v[DM_INST], 0, 15), s, more = 0;
    char b[24];
    if (ui.drm_inst) {                                                /* an instrument held: its sound, its knobs */
        vz_drm_sound(v, (ui.drm_inst - 1u) & 15u);
        if (ui.last < NDIN)
            vz_ktag(118, DLBL - 1, &DRI_P[ui.last], tp[sys.sel].dins[(ui.drm_inst - 1u) & 15u][ui.last]);
        return;
    }
    if (ui.page == 2u) {
        vz_drm_sound(v, i);
        if (f >= 8u && f < 12u)
            vz_ktag(118, DLBL - 1, &DRM_P[f], v[f]);
        return;
    }
    if (ui.page == 3u && v[DM_MODE] != DMODE_LIVE && v[DM_MODE] != DMODE_ERAS) {   /* STEP: the instrument's row */
        uint32_t row = tp[sys.sel].dpat[bar][0], acc = tp[sys.sel].dacc[bar], now = drm_now[sys.sel];
        for (s = 0; s < 16u; s++) {
            int32_t x = DX0 + 1 + (int32_t)s * 7 + (int32_t)(s / 4u) * 2;
            row = tp[sys.sel].dpat[bar][s];
            if ((acc >> s) & 1u)
                px_box(x, DY0 + 2, 5, 2, v[DM_MODE] == DMODE_ACC ? px_ink : px_dim);
            if ((row >> i) & 1u)
                px_box(x, DY0 + 6, 5, 20, v[DM_MODE] == DMODE_ACC ? px_dim : px_ink);
            else
                px_frame(x, DY0 + 6, 5, 20, px_dim, 2);
            if (sys.playing && now == bar * 16u + s)
                px_box(x - 1, DY0 + 28, 7, 2, px_ink);
            if (!(s & 3u)) {
                char n[4];
                fmt_int(n, (int32_t)s + 1);
                px_text(x, DY1 - 1, PXF_3, n, px_dim);
            }
        }
    } else {
        more = vz_drm_grid(v, bar);
    }
    str_cpy(b, ui.page == 1u ? "SEED " : N_DPATN[clamp(v[DM_PATN], 0, (int32_t)DRM_NPRESET - 1)], 8);
    if (ui.page == 1u) {
        if (v[DM_SEED] > 0)
            fmt_int(b + 5, (int32_t)drm_version(v, sys.playing ? drm_pass[sys.sel] : 0u));
        else
            str_cpy(b + 5, "OFF", 4);
    }
    if (ui.page == 3u)
        str_cpy(b, DRM_KIT[i].code, 3);
    str_cpy(b + str_len(b), "  BAR ", 7);
    fmt_int(b + str_len(b), (int32_t)bar + 1);
    if (more) {                                                       /* (rows the grid had no room for) */
        str_cpy(b + str_len(b), "  +", 4);
        fmt_int(b + str_len(b), (int32_t)more);
    }
    px_text(DX0, DLBL, PXF_3, b, px_ink);
    if (f < NPK && !pdesc_empty(&DRM_P[f]))
        vz_ktag(118, DLBL - 1, &DRM_P[f], v[f]);
    else if (ui.page == 3u)
        px_text(118 - px_text_w(PXF_3, N_DMODE[clamp(v[DM_MODE], 0, 3)]), DLBL, PXF_3, N_DMODE[clamp(v[DM_MODE], 0, 3)],
                px_ink);
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
     * at the right of that row. The steps whose white keys are held (KNOB 1 sets them) are framed. */
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
            if ((ui.steps_held >> k) & 1u)                             /* a white key held: KNOB 1 sets this one */
                px_frame(x - 1, top - 1, bw + 2, h + 3, px_ink, 1);
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
 * one response over 8 octaves (ch_resp, ui_px.c: what mixer.c applies), a node on each shelf's corner and
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
    /* the pan: L and R, each side's gain as a bar (mixer.c: equal power, sqrt 2 times cos and sin of the place, at
     * most full, so the centre has both at full) */
    pl = clamp(px_cos((ch[CH_PAN] + 100) * 8 / 100) * 22 * 1414 / 1000000, 0, 22);
    pr = clamp(px_sin((ch[CH_PAN] + 100) * 8 / 100) * 22 * 1414 / 1000000, 0, 22);
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

/* the master compressor (the mixer's MASTER page): in against out from -36 dB to full scale as one line (what
 * mixer.c mx_curve does to a steady sound; the dim diagonal is unity), a node where the threshold sits, and what
 * it takes off now as a bar falling from the top on the right, with its dB */
static void viz_master(uint32_t f)
{
    int32_t w = 88, x, py = 0, gr = comp.gr_q8, h;
    char b[12];
    px_line(DX0, DY1, DX0 + w, DY1 - w * 36 / 42 * DH / w, px_dim, 2);   /* unity: out = in */
    for (x = 0; x <= w; x++) {
        int32_t lv = 15 * 256 - 1531 + x * 1531 / w, g, d, out, y;
        g = mx_curve(lv, &d);
        out = lv + mx_log2((uint32_t)g) - 12 * 256;    /* (g is Q12: log2 minus 12 octaves) */
        y = clamp(DY1 - (out - (15 * 256 - 1531)) * DH / 1787, DY0, DY1);   /* (42 dB of rows: 1787) */
        if (x)
            px_line(DX0 + x - 1, py, DX0 + x, y, px_ink, 1);
        py = y;
    }
    if (mst[MS_AMT]) {                                  /* the threshold */
        int32_t tx = DX0 + w - mst[MS_AMT] * 1276 / 100 * w / 1531, d;
        int32_t g = mx_curve(15 * 256 - mst[MS_AMT] * 1276 / 100, &d), out;
        out = 15 * 256 - mst[MS_AMT] * 1276 / 100 + mx_log2((uint32_t)g) - 12 * 256;
        vz_node(tx, clamp(DY1 - (out - (15 * 256 - 1531)) * DH / 1787, DY0, DY1), f == 0u);
    }
    h = clamp(gr * DH / 512, 0, DH);                     /* (12 dB, 2 octaves, the whole height) */
    px_frame(103, DY0, 7, DH + 1, px_dim, 2);
    if (h)
        px_box(104, DY0 + 1, 5, h, px_ink);
    str_cpy(b, "-", sizeof b);                          /* "-3.2": dB off now */
    fmt_int(b + 1, gr * 602 / 25600);
    str_cpy(b + str_len(b), ".", 2);
    fmt_int(b + str_len(b), gr * 602 / 2560 % 10);
    px_text_c(97, 20, DLBL, PXF_3, gr ? b : "0", px_ink);
    vz_label(DX0 + w / 2, "IN : OUT", 0);
    if (f < 4u)
        vz_ktag(92, DY0 - 1, &MS_P[f], mst[f]);
}

/* tenths of a second as "4.2" (the unit goes apart: an S after digits reads as a 5 in these faces) */
static void vz_secs(char *b, int32_t tenths)
{
    fmt_int(b, tenths / 10);
    str_cpy(b + str_len(b), ".", 2);
    fmt_int(b + str_len(b), tenths % 10);
}

/* chunks as tenths of a second of tape-format sound */
static int32_t vz_chunk_tenths(uint32_t n) { return (int32_t)(n * TAPE_CHS * 10u / TAPE_SR); }

/* The mixer's levels page: what each track holds of the shared memory, under its fader: its tape's seconds and,
 * when GRAIN is on, its live buffer's (GR); a parked track says OFF, its tape (kept, taken first) dim. Then the
 * memory as one ribbon (each track's tape solid, its GRAIN buffer and strings dim, what's free dotted), and under it TRACKS and
 * the time still free. SELECT sets TRACKS here. */
static void viz_memory(void)
{
    char b[20];
    uint32_t t, x0 = DX0, c;
    for (t = 0; t < NTRK; t++) {
        int32_t cx = 30 * (int32_t)t, off = t >= sys.ntrk;
        uint32_t nt = tape_ctl[t].nch, ng = mem_count(MEM_GRAIN + t);
        uint16_t col = off ? px_dim : px_ink;
        if (off)
            px_text_c(cx, 30, 3, PXF_5, "OFF", px_dim);
        if (nt) {
            vz_secs(b, vz_chunk_tenths(nt));
            px_text_c(cx, 30, off ? 13 : 3, off ? PXF_3 : PXF_5, b, col);
            if (!off)
                px_text_c(cx, 30, 12, PXF_3, "SEC", px_dim);
        } else if (!off) {
            px_text_c(cx, 30, 3, PXF_5, "-", px_ink);
        }
        if (ng && !off) {
            str_cpy(b, "GR ", sizeof b);
            vz_secs(b + 3, vz_chunk_tenths(ng));
            px_text_c(cx, 30, 18, PXF_3, b, px_ink);
        }
    }
    px_line(DX0, 27, DX1, 27, px_dim, 2);              /* the ribbon: free memory dotted under the rest */
    for (t = 0; t < NTRK; t++) {
        uint32_t nt = mem_count(MEM_TAPE + t), ng = mem_count(MEM_GRAIN + t) + mem_count(MEM_RESO + t) +
                 mem_count(MEM_SPACE + t), w0 = x0;
        uint32_t wt = nt * (uint32_t)DW / MEM_NC, wg = ng * (uint32_t)DW / MEM_NC;
        if (nt && !wt)
            wt = 1;
        if (ng && !wg)
            wg = 1;
        if (wt)
            px_box((int32_t)x0, 25, (int32_t)wt, 5, t >= sys.ntrk ? px_dim : px_ink);
        x0 += wt;
        if (wg)
            px_box((int32_t)x0, 25, (int32_t)wg, 5, px_dim);
        x0 += wg;
        if (x0 - w0 >= 6u) {                             /* its number under it, where it fits */
            b[0] = (char)('1' + t);
            b[1] = 0;
            px_text_c((int32_t)w0, (int32_t)(x0 - w0), 31, PXF_3, b, px_ink);
        }
        if (x0 > w0)
            x0++;                                       /* (a gap between tracks) */
    }
    c = mem_count(MEM_IMPORT);
    if (c)
        px_box((int32_t)x0, 25, (int32_t)(c * (uint32_t)DW / MEM_NC + 1u), 5, px_dim);
    str_cpy(b, "TRACKS ", sizeof b);
    fmt_int(b + 7, sys.ntrk);
    px_tag(1, DLBL - 1, PXF_3, b, px_ink, px_bg);
    str_cpy(b, "FREE ", sizeof b);                     /* the time free, at the ribbon's free end */
    vz_secs(b + 5, vz_chunk_tenths(mem_count(MEM_FREE)));
    str_cpy(b + str_len(b), " SEC", 5);
    px_box(118 - px_text_w(PXF_3, b) - 2, 30, px_text_w(PXF_3, b) + 3, 7, px_bg);
    px_text(118 - px_text_w(PXF_3, b), 31, PXF_3, b, px_ink);
    str_cpy(b, "CPU ", sizeof b);                      /* the audio ISR's load (audio.c), in 5 % steps, lit from 85 %
                                                        * (where shedding starts) */
    fmt_int(b + 4, (int32_t)(sys.cpu_q8 * 100u / 256u / 5u * 5u));
    str_cpy(b + str_len(b), "%", 2);
    px_text(118 - px_text_w(PXF_3, b), DLBL, PXF_3, b, sys.cpu_q8 * 100u / 256u >= 85u ? px_ink : px_dim);
}

/* The routing view (ALGORITHM): what each track's REC records. The tracks are boxes under their knobs (filled while
 * REC is armed, dim when parked); a line runs from the track heard down into a lane of the recording track's own and
 * up into it, an arrow at its end; solid while that REC is armed, dotted while it isn't. The others' mix (AUTO on a
 * TAPE track, OTHR) and AUTO on a SYNTH or POLY track (its own source) are named in the lane instead. SELF: a loop
 * under the box. Under it all, the last-turned (or the
 * focused) track's routing in words. */
static void viz_route(uint32_t f)
{
    static const char *const SRC_SHORT[NSRC] = {"TAPE", "SYNTH", "POLY"};
    uint32_t k, j, w = f < 4u ? f : sys.sel;
    char b[28];
    for (k = 0; k < NTRK; k++) {                         /* the tracks */
        int32_t cx = 30 * (int32_t)k + 15, off = k >= sys.ntrk, armed = !off && ((sys.rec >> k) & 1u);
        char n[3] = {'T', (char)('1' + k), 0};
        if (armed) {
            px_box(cx - 8, 1, 17, 9, px_ink);
            px_text_c(cx - 8, 17, 3, PXF_3, n, px_bg);
        } else {
            px_frame(cx - 8, 1, 17, 9, off ? px_dim : px_ink, 1);
            px_text_c(cx - 8, 17, 3, PXF_3, n, off ? px_dim : px_ink);
        }
    }
    for (k = 0; k < NTRK && k < sys.ntrk; k++) {         /* each recording track's lane: what it hears */
        uint32_t from = tp[k].recin, st = (sys.rec >> k) & 1u ? 1u : 2u;
        int32_t y = 19 + 4 * (int32_t)k, ck = 30 * (int32_t)k + 15;   /* (words under the box, lanes below) */
        uint16_t c = st == 1u ? px_ink : px_dim;
        if (from == RIN_AUTO || from == RIN_OTHR)       /* (in words, below) */
            continue;
        if (from == RIN_T1 + k) {                        /* SELF: round under its box and back in */
            px_line(ck - 4, 11, ck - 4, y, c, st);
            px_line(ck - 4, y, ck + 4, y, c, st);
            px_line(ck + 4, y, ck + 4, 11, c, st);
            px_dot(ck + 3, 12, c);
            px_dot(ck + 5, 12, c);
            continue;
        }
        for (j = 0; j < NTRK; j++) {
            int32_t xs, xa;
            if (j == k || j >= sys.ntrk || (from >= RIN_T1 && from - RIN_T1 != j))
                continue;
            xs = 30 * (int32_t)j + 15 + 2 * (int32_t)k - 3;   /* (each lane leaves a box at its own column) */
            xa = ck + 2 * (int32_t)j - 3;
            px_line(xs, 11, xs, y, c, st);
            px_line(xs, y, xa, y, c, st);
            px_line(xa, y, xa, 11, c, st);
            px_dot(xa - 1, 12, c);                       /* the arrow's head, into the box */
            px_dot(xa + 1, 12, c);
        }
    }
    for (k = 0; k < NTRK && k < sys.ntrk; k++) {         /* the others' mix (AUTO on a TAPE track, OTHR) and AUTO's
                                                         * own source in words: lines from every box would crowd out
                                                         * the routes you've set; over any line passing under */
        uint32_t from = tp[k].recin;
        const char *wd = from == RIN_AUTO && tp[k].src != SRC_TAPE ? SRC_SHORT[tp[k].src % NSRC]
                       : from == RIN_AUTO || from == RIN_OTHR ? "OTHERS" : 0;
        int32_t cx = 30 * (int32_t)k;
        if (!wd)
            continue;
        px_box(cx + 15 - px_text_w(PXF_3, wd) / 2 - 1, 11, px_text_w(PXF_3, wd) + 2, 7, px_bg);
        px_text_c(cx, 30, 12, PXF_3, wd, (sys.rec >> k) & 1u ? px_ink : px_dim);
    }
    b[0] = 'T';                                          /* the routing in words */
    b[1] = (char)('1' + w);
    b[2] = 0;
    if (w >= sys.ntrk)
        str_cpy(b + 2, " IS OFF (TRACKS)", sizeof b - 2);
    else if (tp[w].recin == RIN_AUTO && tp[w].src != SRC_TAPE)
        str_cpy(b + 2, tp[w].src == SRC_SYNTH ? " RECORDS ITS SYNTH" : " RECORDS ITS POLY", sizeof b - 2);
    else if (tp[w].recin == RIN_AUTO || tp[w].recin == RIN_OTHR)
        str_cpy(b + 2, " RECORDS THE OTHERS", sizeof b - 2);
    else if (tp[w].recin == RIN_T1 + w)
        str_cpy(b + 2, " RECORDS ITSELF", sizeof b - 2);
    else {
        str_cpy(b + 2, " RECORDS T", sizeof b - 2);
        b[str_len(b) + 1] = 0;
        b[str_len(b)] = (char)('1' + tp[w].recin - RIN_T1);
    }
    if (f < 4u)
        px_tag(1, DLBL - 1, PXF_3, b, px_ink, px_bg);
    else
        px_text(2, DLBL, PXF_3, b, px_ink);
}

/* --------------------------------------------------- USB record mode --- */
/* The strip: the four tracks a take can go to, each its number in a box, and what its tape holds now (seconds, "-"
 * empty, OFF parked). Picking one (CHOOSE) fills its box and says REPLACE under it, where SEC was. */
/* seconds of n blocks, two decimals ("12.34") */
static void ur_secs2(char *b, uint32_t n)
{
    uint32_t cs = n * TAPE_BLK * 100u / TAPE_SR;
    fmt_int(b, (int32_t)(cs / 100u));
    str_cpy(b + str_len(b), ".", 2);
    b[str_len(b) + 1] = 0;
    b[str_len(b)] = (char)('0' + cs / 10u % 10u);
    b[str_len(b) + 1] = 0;
    b[str_len(b)] = (char)('0' + cs % 10u);
}

/* CHOOSE's knobs: START and LENGTH (seconds), GAIN (dB, NORM), FADE (ms), each with its pictogram */
static void viz_usbrec_knobs(void)
{
    static const char *const NAME[4] = {"STRT", "LEN", "GAIN", "FADE"};
    static const uint8_t PK[4] = {PK_START, PK_LENGTH, PK_FADER, PK_FADE};
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        pdesc_t d = {NAME[k], 0, 1, 0, F_NUM, 0};
        int32_t x = 30 * (int32_t)k, v = ur.kv[k];
        char b[12];
        if (k <= UK_LEN) {
            d.max = (int16_t)(ur.nblk ? ur.nblk : 1u);
            ur_secs2(b, (uint32_t)v);
        } else if (k == UK_GAIN) {
            d.min = -24;
            d.max = UR_NORM;
            if (v == UR_NORM) {
                str_cpy(b, "NORM", sizeof b);
            } else {
                str_cpy(b, v > 0 ? "+" : "", sizeof b);
                fmt_int(b + str_len(b), v);
                str_cpy(b + str_len(b), "DB", 3);
            }
        } else {
            d.max = 100;
            fmt_int(b, v * UR_FADE_MS);
            str_cpy(b + str_len(b), "MS", 3);
        }
        px_picto(PK[k], x + 4, 2, &d, v, px_ink);
        if (ui.last == k)
            px_tag(x + (30 - px_text_w(PXF_5, NAME[k])) / 2 - 1, 26, PXF_5, NAME[k], px_ink, px_bg);
        else
            px_text_c(x, 30, 27, PXF_5, NAME[k], px_ink);
        px_text_c(x, 30, 37, PXF_5, b, px_ink);
    }
}

static void viz_usbrec_strip(void)
{
    uint32_t k;
    char b[12];
    if (ur.state == UR_CHOOSE) {
        viz_usbrec_knobs();
        return;
    }
    for (k = 0; k < NTRK; k++) {
        int32_t x = 30 * (int32_t)k, off = k >= sys.ntrk, pick = ur.state == UR_CHOOSE && ur.dest == (int8_t)k;
        char n[2] = {(char)('1' + k), 0};
        uint16_t c = off ? px_dim : px_ink;
        if (pick) {
            px_box(x + 7, 2, 16, 20, px_ink);
            px_text_big(x + 10, 5, 2, n, px_bg);
        } else {
            px_frame(x + 7, 2, 16, 20, c, off ? 2 : 1);
            px_text_big(x + 10, 5, 2, n, c);
        }
        if (off) {
            px_text_c(x, 30, 27, PXF_5, "OFF", px_dim);
            continue;
        }
        if (tape_ctl[k].nblk && !tape_ctl[k].empty) {
            vz_secs(b, (int32_t)(tape_ctl[k].nblk * TAPE_BLK * 10u / TAPE_SR));
            px_text_c(x, 30, 27, PXF_5, b, px_ink);
            if (!pick)
                px_text_c(x, 30, 36, PXF_3, "SEC", px_dim);
        } else {
            px_text_c(x, 30, 27, PXF_5, "-", px_ink);
        }
        if (pick)
            px_text_c(x, 30, 36, PXF_3, "REPLACE", px_ink);
    }
}

/* the memory as one ribbon at row y: the other tracks' tapes dim (not the one the take replaces), the take in ink
 * (what would be kept), what's left a dotted line */
static void ur_ribbon(int32_t y, uint32_t take)
{
    uint32_t t, x = DX0;
    for (t = 0; t < NTRK; t++) {
        uint32_t n = (ur.state == UR_CHOOSE && ur.dest == (int8_t)t) ? 0u : tape_ctl[t].nch, w = n * (uint32_t)DW / MEM_NC;
        if (n && !w)
            w = 1;
        if (w) {
            px_box((int32_t)x, y, (int32_t)w, 3, px_dim);
            x += w + 1u;
        }
    }
    if (take) {
        uint32_t w = take * (uint32_t)DW / MEM_NC + 1u;
        px_box((int32_t)x, y - 1, (int32_t)w, 5, px_ink);
        x += w + 1u;
    }
    if ((int32_t)x < DX1)
        px_line((int32_t)x, y + 1, DX1, y + 1, px_dim, 2);
}

/* CHOOSE: the take's outline (the kept part in ink, the rest dim, the cut points and where the loop plays), the
 * ribbon, what's kept and what's left, and where it goes */
static void viz_usbrec_choose(void)
{
    uint32_t n = ur.nblk ? ur.nblk : 1u, s = (uint32_t)ur.kv[UK_STRT], e = s + (uint32_t)ur.kv[UK_LEN], c, py = 0;
    int32_t x;
    char b[32], t[12];
    for (x = 0; x <= DW; x++) {                                    /* the outline: one line, no fill */
        uint32_t b0 = (uint32_t)x * n / (DW + 1u), b1 = ((uint32_t)x + 1u) * n / (DW + 1u), pk = 0, y;
        if (b1 <= b0)
            b1 = b0 + 1u;
        for (c = b0; c < b1 && c < ur.nblk; c++) {
            uint32_t p = ur_chunk(c)->peak[c % MEM_CB];
            pk = p > pk ? p : pk;
        }
        y = 16u - (pk > 255u ? 255u : pk) * 15u / 255u;
        if (x)
            px_line(DX0 + x - 1, (int32_t)py, DX0 + x, (int32_t)y, b0 >= s && b0 < e ? px_ink : px_dim, b0 >= s && b0 < e ? 1 : 2);
        py = y;
    }
    px_line(DX0 + (int32_t)(s * (DW + 1u) / n), 1, DX0 + (int32_t)(s * (DW + 1u) / n), 17, px_ink, 1);
    px_line(DX0 + (int32_t)(e * (DW + 1u) / n) - 1, 1, DX0 + (int32_t)(e * (DW + 1u) / n) - 1, 17, px_ink, 1);
    x = DX0 + (int32_t)((ur.pp >> 12) / TAPE_BLK * (DW + 1u) / n);   /* where the loop plays */
    px_box(x, 18, 1, 2, px_ink);
    ur_ribbon(23, ur_keep_chunks());
    ur_secs2(t, (uint32_t)ur.kv[UK_LEN]);
    str_cpy(b, "KEEP ", sizeof b);
    str_cpy(b + str_len(b), t, 8);
    px_text(DX0, 28, PXF_5, b, px_ink);
    vz_secs(b, vz_chunk_tenths(ur_free_after()));                 /* what the other tracks would still have */
    str_cpy(b + str_len(b), " SEC LEFT", 10);
    px_text(DX1 - px_text_w(PXF_3, b) + 1, 30, PXF_3, b, px_dim);
    if (ur.dest < 0) {
        str_cpy(b, "WHICH TRACK? WHITE KEYS 1-", sizeof b);
        fmt_int(b + str_len(b), sys.ntrk);
    } else {
        str_cpy(b, "ONTO TRACK ", sizeof b);
        fmt_int(b + str_len(b), ur.dest + 1);
        if (tape_ctl[(uint32_t)ur.dest].nblk && !tape_ctl[(uint32_t)ur.dest].empty) {
            str_cpy(b + str_len(b), ", REPLACING ", 13);
            vz_secs(t, (int32_t)(tape_ctl[(uint32_t)ur.dest].nblk * TAPE_BLK * 10u / TAPE_SR));
            str_cpy(b + str_len(b), t, 8);
        }
    }
    px_text(DX0, DLBL, PXF_3, b, ur.dest < 0 ? px_dim : px_ink);
}

/* The panel: what the step is, the time large, and one level bar (both sides' peak), plain. READY: the room a take
 * has, or a note to play something on the computer; RECORDING: the take's seconds of the room, and the memory as a
 * ribbon (the take growing against the other tracks); CHOOSE: viz_usbrec_choose. */
static void viz_usbrec(void)
{
    char b[28], t[12];
    int32_t pk = ur.peak, w;
    uint32_t room = ur_room() * TAPE_CHS * 10u / TAPE_SR;          /* tenths */
    ur.peak = pk - (pk >> 2);                                      /* (the bar falls between frames) */
    if (ur.state == UR_CHOOSE) {
        viz_usbrec_choose();
        return;
    }
    if (ur.state == UR_READY) {
        px_text(DX0, 2, PXF_5B, "READY", px_ink);
        vz_secs(t, (int32_t)room);
        str_cpy(b, "ROOM FOR ", sizeof b);
        str_cpy(b + str_len(b), t, 8);
        str_cpy(b + str_len(b), " SEC", 5);
        px_text(DX0, 14, PXF_5, b, px_ink);
    } else {                                                       /* RECORDING */
        vz_secs(t, (int32_t)usbrec_tenths());
        px_text_big(DX0, 1, 3, t, px_ink);
        w = DX0 + 18 * (int32_t)str_len(t);
        vz_secs(b + 3, (int32_t)room);
        b[0] = 'O';
        b[1] = 'F';
        b[2] = ' ';
        px_text(w, 9, PXF_3, "SEC", px_ink);
        px_text(w, 16, PXF_3, b, px_dim);
        ur_ribbon(24, ur.nch);
    }
    if (!usbrec_live()) {                                          /* nothing arriving */
        px_text(DX0, 28, PXF_3, "PLAY SOUND ON THE COMPUTER", px_dim);
        px_text(DX0, 35, PXF_3, "WITH BRYO AS ITS OUTPUT", px_dim);
        return;
    }
    w = pk * (DW + 1) / 32767;                                     /* the level: one bar, a tick at full scale */
    px_frame(DX0, 30, DW + 1, 5, px_dim, 2);
    if (w > 0)
        px_box(DX0, 31, w > DW ? DW : w, 3, px_ink);
    px_text(DX0, DLBL, PXF_3, "LEVEL", px_dim);
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
    if (ui.view == VIEW_USBREC) {                                      /* the step, the time, the level, the room */
        uint32_t tn = ur.state == UR_RECORDING ? usbrec_tenths() : ur.nblk;
        if (ur.state == UR_CHOOSE) {                                   /* the knobs, the loop's place, the memory */
            for (k = 0; k < 4u; k++)
                h = (h ^ (uint32_t)(ur.kv[k] + 32768)) * 16777619u;
            h = (h ^ ((ur.pp >> 12) / TAPE_BLK * (DW + 1u) / (ur.nblk ? ur.nblk : 1u))) * 16777619u;
            for (t = 0; t < NTRK; t++)
                h = (h ^ tape_ctl[t].nch) * 16777619u;
            h += mem_count(MEM_FREE) * 2654435761u;
        }
        return h + ur.state * 31u + tn * 131u + (uint32_t)(ur.dest + 1) * 7u + (uint32_t)usbrec_live() * 65537u +
               (uint32_t)(ur.peak * 24 / 32767) * 524287u + ur_room() * 8191u + sys.ntrk * 3u;
    }
    if (ui.view == VIEW_MIXER) {
        for (t = 0; t < NCH; t++)
            h = (h ^ (uint32_t)(tp[sys.sel].ch[t] + 32768)) * 16777619u;
        if (!ui.chan)                                                  /* the memory: who holds how much */
            for (k = 0; k < MEM_NC; k++)
                h = (h ^ mem_owner[k]) * 16777619u;
        if (ui.chan == CHAN_MASTER)                                    /* the compressor's knobs and what it takes off */
            for (k = 0; k < NMS; k++)
                h = (h ^ (uint32_t)(mst[k] + 32768)) * 16777619u;
        return h + ui.chan * 977u + sys.ntrk * 5381u + (ui.chan ? 0u : sys.cpu_q8 * 100u / 256u / 5u * 7919u) +
               (ui.chan == CHAN_MASTER ? (uint32_t)comp.gr_q8 * 602u / 2560u * 104729u : 0u);
    }
    if (ui.view == VIEW_ROUTE) {
        for (t = 0; t < NTRK; t++)
            h = (h ^ (uint32_t)(tp[t].recin + tp[t].src * 16u)) * 16777619u;
        return h + sys.rec * 977u + sys.ntrk * 5381u + 0x55u;
    }
    for (k = 0; k < NPK; k++) {                                        /* both pages: page 2 shows in the picture */
        const int16_t *v = ui.kind == FOCUS_SLOT ? tp[sys.sel].mod[ui.slot] : dev_v(sys.sel, ui.dev);
        h = (h ^ (uint32_t)(v[k] + 32768)) * 16777619u;
    }
    h += ui.page * 389u;
    if (ui.kind == FOCUS_SLOT && tp[sys.sel].engine[ui.slot] == ME_SEQ)   /* SEQ draws its step values */
        for (k = 0; k < 16u; k++)
            h = (h ^ (uint32_t)(uint8_t)tp[sys.sel].steps[ui.slot][k]) * 16777619u + ui.steps_held * 7u;
    if (ui.kind == FOCUS_DEV && ui.dev == DEV_SRC)                     /* the source; the keys' range: the octave */
        h = (h ^ (tp[sys.sel].src * 7u + track[sys.sel].octave * 131u)) * 16777619u;
    if (ui.kind == FOCUS_DEV && ui.dev == DEV_SRC && tp[sys.sel].src == SRC_DRUM) {   /* DRUM: the pattern, the
                                                                                     * playhead, an instrument held */
        const track_params_t *P = &tp[sys.sel];
        for (k = 0; k < DRM_NBAR * 16u; k++)
            h = (h ^ P->dpat[k >> 4][k & 15u]) * 16777619u;
        for (k = 0; k < DRM_NBAR; k++)
            h = (h ^ P->dacc[k]) * 16777619u;
        for (k = 0; k < DRM_NINST * NDIN; k++)
            h = (h ^ (uint32_t)(P->dins[k / NDIN][k % NDIN] + 32768)) * 16777619u;
        h = (h ^ (ui.drm_inst + (sys.playing ? drm_now[sys.sel] * 31u + drm_pass[sys.sel] * 7919u : 0u))) * 16777619u;
    }
    if (ui.kind == FOCUS_DEV && ui.dev == DEV_SRC && tp[sys.sel].src == SRC_POLY)   /* POLY on the track's tape */
        h = (h ^ (tape_ver[sys.sel] * 31u)) * 16777619u;
    if (ui.kind == FOCUS_DEV && ((ui.dev == DEV_SRC && tp[sys.sel].src == SRC_TAPE) || ui.dev == DEV_GRAIN))   /* the tape, the head */
        h = (h ^ (tape_ver[sys.sel] * 31u + (uint32_t)(tape_head(sys.sel) * DW / 1000 + 7) + sys.rec * 977u)) * 16777619u;
    if (ui.kind == FOCUS_SLOT && tp[sys.sel].engine[ui.slot] == ME_FOLLOW)
        for (t = 0; t < NTRK; t++)
            h = (h ^ tape_ver[t]) * 16777619u;
    if (ui.kind == FOCUS_DEV && ui.dev == DEV_GRAIN) {                 /* GRAIN: its cursor, FROZEN, the grains */
        const grain_trk_t *G = &grain[sys.sel];
        const gr_buf_t *B = &gbuf[sys.sel];
        uint8_t sh[NTRK];
        gr_plan_into(sh);                                              /* (its share: other tracks move it too) */
        h = (h ^ (uint32_t)((G->cur >> 12) / 256 * 2 + grain_frozen + sh[sys.sel] * 4096u)) * 16777619u;
        h = (h ^ (uint32_t)(B->len + B->bars * 7u + (uint32_t)B->w / 1024u * 131u)) * 16777619u;
        for (k = 0; k < GR_SLOTS; k++)
            if (((gr_used >> k) & 1u) && gslot[k].trk == sys.sel)
                h = (h ^ (uint32_t)(gslot[k].pos >> 18) ^ k << 24) * 16777619u;
    }
    if (ui.kind == FOCUS_DEV && ui.dev == DEV_RESO)                    /* RESONATOR: a key's root */
        h = (h ^ (uint32_t)reso_root16(sys.sel)) * 16777619u;
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
    px_begin(VZ_H);
    if (ui.msg_t) {
        viz_message();
    } else if (ui.view == VIEW_USBREC) {
        viz_usbrec();
    } else if (ui.view == VIEW_MIXER) {
        if (ui.chan == CHAN_MASTER)
            viz_master(f);
        else if (ui.chan)
            viz_channel(f);
        else
            viz_memory();
    } else if (ui.view == VIEW_ROUTE) {
        viz_route(ui.last < 4u ? ui.last : 0xFFu);
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
                else if (tp[sys.sel].src == SRC_DRUM)
                    viz_drum(v, f);
                else
                    viz_tape(v, f);
                break;
            case DEV_GRAIN: viz_grain(v, f); break;
            case DEV_RESO: {                                    /* (a key's root shows as PTCH's) */
                int16_t rv[NPK];
                uint32_t k;
                for (k = 0; k < NPK; k++)
                    rv[k] = v[k];
                rv[RP_PTCH] = (int16_t)(reso_root16(sys.sel) / 16);
                viz_reso(rv, f);
                break;
            }
            case DEV_COLOR: viz_color(v, f); break;
            default: viz_space(v, f); break;
            }
        }
    }
    px_blit(UI_VIZ_Y);
}

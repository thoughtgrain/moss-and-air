/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: the USB record mode (docs/bryo-architecture.md, "USB record mode"): the computer plays into the FM-1 over
 * the cable (usb.c, BRYO_UAC_OUT: the FM-1 is one of its sound outputs) and Bryo records it onto a track.
 *
 * REC held a second, the transport stopped, opens it. Nothing else runs while it's up: the transport stops, every
 * track's REC lets go, the four tracks stop rendering (their GRAIN buffers, RESONATOR strings and SPACE lines go back
 * to the shared memory, so the take can have them), and the FM-1's output plays what the computer sends, so you hear
 * what's being recorded. The FM-1's own USB input (what the computer can record from it) sends silence meanwhile, so
 * nothing loops round. Then:
 *
 *   READY     REC starts the take; HOME leaves
 *   RECORDING REC stops it; HOME stops and throws it away
 *   CHOOSE    the take loops so you hear what you'd keep, and KNOB 1-4 shape it: START and LENGTH trim it (only the
 *             kept part takes memory: the rest goes back as it's kept), GAIN in dB (or NORM: the loudest point to
 *             full scale), FADE at both cut points (so a cut mid-sound doesn't click). White keys 1..TRACKS pick the
 *             track; REC puts the take on it (its tape replaced) and leaves; HOME throws the take away (READY)
 *
 * Who does what: TIMER5 (usb.c uaco_service, through uaco_frames below) takes each 1 ms packet and writes two rings,
 * the frames as they came for the monitor, and while recording, the take as mono at 22,050 Hz (the tape's rate: two
 * frames into one sample, as a WAV over the drive). The audio ISR plays the monitor's ring (the DAC's clock isn't
 * the computer's, so it reads at a rate a hair off one that keeps the ring near a set fill: no clicks, the pitch off
 * by at most 1 %, about 0.04 % in practice). The main loop encodes the take into the tape's format, in chunks of the
 * shared memory it takes as it goes (free ones, cleared and parked tapes': never a tape in use), so recording itself
 * never drops or repeats a sample whatever the two clocks do. A take is a tape's most, 23.6 s, or what memory there
 * is; when either runs out it stops there and goes on to CHOOSE. The screen shows what a take costs: the memory
 * as one ribbon (the other tracks' tapes, the take, what's left), so a 20 s take that would leave the other tracks
 * nothing says so before it's kept, and trimming it shows the room coming back.
 *
 * Keeping a trimmed take moves its blocks down to the start of its chunks (each block decodes on its own, so a
 * plain copy); only the blocks GAIN or a FADE changes are decoded and encoded again (once: the rest keep their
 * bits), and the chunks past the end go back to the pool.
 *
 * Integer only. */

#define UR_MON 1024u                 /* the monitor's ring, stereo frames (23 ms): TIMER5 -> the audio ISR */
#define UR_MON_AIM 192u              /* .. its fill kept about here (4.4 ms) */
#define UR_REC 4096u                 /* the take's ring, mono samples at 22,050 Hz (186 ms): TIMER5 -> the main loop */
enum { UR_OFF, UR_READY, UR_RECORDING, UR_CHOOSE, UR_KEEPING };
enum { UK_STRT, UK_LEN, UK_GAIN, UK_FADE };   /* CHOOSE's knobs */
#define UR_NORM 25                   /* GAIN past +24 dB: NORM */
#define UR_FADE_MS 5                 /* FADE's step (0..100: 0..500 ms) */

static uint32_t ur_mon[UR_MON] __attribute__((section(".pool")));   /* (the 16 KB the pool kept for USB audio in: */
static volatile uint32_t ur_mw, ur_mr;                                /*  these two take 12) */
static int16_t ur_rec[UR_REC] __attribute__((section(".pool")));
static volatile uint32_t ur_rw, ur_rr;

static struct {
    volatile uint8_t state;          /* UR_* (the main loop's; TIMER5 reads it) */
    volatile uint8_t feed;           /* TIMER5 writes the take's ring */
    int8_t dest;                     /* CHOOSE: the track picked, -1 none yet */
    /* TIMER5's */
    int32_t acc;                     /* the first frame of a pair (L + R) */
    uint8_t odd;
    volatile uint32_t frames;        /* frames arrived since the mode opened (is the computer playing?) */
    volatile int32_t peak;           /* the input's peak since the screen last looked */
    volatile uint32_t lost;          /* take samples the ring had no room for (the main loop fell behind) */
    /* the audio ISR's */
    volatile uint8_t isr_in;         /* the ISR plays the monitor (1) or the tracks (0) */
    uint8_t primed;                  /* the monitor's ring has filled to its aim since it last ran dry */
    uint32_t pos;                    /* the read point between two frames, Q16 */
    int32_t g;                       /* the monitor's level, Q15: up in a block from silence, down in a block to it */
    int32_t lastl, lastr;            /* the last frame played (it fades out when the computer stops) */
    /* the main loop's: the take */
    uint8_t map[TAPE_MAXCH];
    uint8_t nch;
    int16_t blk[TAPE_BLK];
    uint32_t bn, nblk, pk;
    int32_t pred, idx;
    uint8_t cut;                     /* memory ran out: the take stopped there */
    uint32_t seen_frames, seen_ms;   /* when frames last arrived (the screen) */
    /* CHOOSE: the knobs (START and LENGTH in blocks, GAIN dB or UR_NORM, FADE in UR_FADE_MS steps) and what they
     * make (main loop -> ISR, whole words): the kept samples [pv_s, pv_e), the gain (Q12), the fades' length */
    int16_t kv[4];
    volatile uint32_t pv_s, pv_e, pv_f;
    volatile int32_t pv_g;
    volatile uint8_t take_gen;       /* a new take (the ISR's reader starts over) */
    /* the ISR's preview */
    uint32_t pp;                     /* where it plays, Q12 samples of the take */
    uint8_t rd_gen;
    tape_rd_t rd;
} ur;

/* ---------------------------------------------------------- TIMER5 --- */
/* usb.c: a packet's frames (16-bit L, R little endian), as they arrive. Outside the mode they're dropped. */
static void uaco_frames(const uint8_t *p, uint32_t nframes)
{
    uint32_t i;
    int32_t pk = ur.peak;
    if (ur.state == UR_OFF)
        return;
    for (i = 0; i < nframes; i++, p += 4) {
        int32_t l = (int16_t)(p[0] | p[1] << 8), r = (int16_t)(p[2] | p[3] << 8), a = l < 0 ? -l : l;
        if (ur_mw - ur_mr < UR_MON) {                   /* (full: the ISR isn't reading yet; the newest waits) */
            ur_mon[ur_mw % UR_MON] = (uint32_t)(uint16_t)l | (uint32_t)(uint16_t)r << 16;
            RING_PUBLISH();
            ur_mw++;
        }
        if (a > pk)
            pk = a;
        a = r < 0 ? -r : r;
        if (a > pk)
            pk = a;
        if (!ur.feed)
            continue;
        if (!ur.odd) {                                  /* two frames, both sides, into one sample at 22,050 Hz */
            ur.acc = l + r;
            ur.odd = 1;
            continue;
        }
        ur.odd = 0;
        if (ur_rw - ur_rr < UR_REC) {
            ur_rec[ur_rw % UR_REC] = (int16_t)((ur.acc + l + r) >> 2);
            RING_PUBLISH();
            ur_rw++;
        } else {
            ur.lost++;
        }
    }
    ur.peak = pk;
    ur.frames += nframes;
}

/* ------------------------------------------------------- audio ISR --- */
/* the monitor into l, r (n samples): the computer's frames, read a hair faster or slower than they come so the ring
 * stays near UR_MON_AIM; leaving (sys.usbrec let go) it fades out over this block and the tracks come back next */
static void usbrec_block(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i, fill = ur_mw - ur_mr, step;
    int32_t g = ur.g, dg = sys.usbrec ? 1024 : -1024;
    if (ur.state == UR_CHOOSE && ur.nblk) {             /* CHOOSE: the take as it would be kept, looping */
        tape_view_t v;
        uint32_t s0 = ur.pv_s, e0 = ur.pv_e, fn = ur.pv_f;
        int32_t gq = ur.pv_g;
        memset(&v, 0, sizeof v);
        v.map = ur.map;
        v.len = ur.nblk * TAPE_BLK;
        v.ram = 1;
        if (ur.rd_gen != ur.take_gen) {
            ur.rd_gen = ur.take_gen;
            ur.rd.ok = 0;
            ur.pp = s0 << 12;
        }
        ur_mr = ur_mw;                                  /* (the computer isn't heard meanwhile) */
        ur.primed = 0;
        for (i = 0; i < n; i++) {
            uint32_t j;
            int32_t x, a;
            if (ur.pp < s0 << 12 || ur.pp >= e0 << 12)
                ur.pp = s0 << 12;
            j = ur.pp >> 12;
            x = tape_read(&v, &ur.rd, (int32_t)ur.pp);
            a = (int32_t)(j - s0) < (int32_t)(e0 - 1u - j) ? (int32_t)(j - s0) : (int32_t)(e0 - 1u - j);
            if (fn && (uint32_t)a < fn)                 /* the fades at the cut points */
                x = x * a / (int32_t)fn;
            x = clamp((x * gq) >> 12, -32767, 32767);
            ur.pp += 2048u;                             /* (22,050 Hz at 44,100: half a sample a sample) */
            g = clamp(g + dg, 0, 32767);
            l[i] = r[i] = (x * g) >> 15;
            ur.lastl = ur.lastr = x;
        }
        ur.g = g;
        if (!sys.usbrec && !g)
            ur.isr_in = 0;
        return;
    }
    if (!ur.primed && fill >= UR_MON_AIM)
        ur.primed = 1;
    step = 65536u + (uint32_t)clamp(((int32_t)fill - (int32_t)UR_MON_AIM) * 4, -655, 655);   /* within 1 % */
    for (i = 0; i < n; i++) {
        int32_t a, b;
        if (ur.primed && ur_mw - ur_mr >= 2u) {
            uint32_t f0 = ur_mon[ur_mr % UR_MON], f1 = ur_mon[(ur_mr + 1u) % UR_MON], fr = ur.pos >> 4;
            int32_t l0 = (int16_t)(f0 & 0xFFFFu), l1 = (int16_t)(f1 & 0xFFFFu);
            int32_t r0 = (int16_t)(f0 >> 16), r1 = (int16_t)(f1 >> 16);
            a = l0 + (((l1 - l0) * (int32_t)fr) >> 12);
            b = r0 + (((r1 - r0) * (int32_t)fr) >> 12);
            ur.pos += step;
            ur_mr += ur.pos >> 16;
            ur.pos &= 0xFFFFu;
        } else {                                        /* the computer stopped (or hasn't started): what was last
                                                         * heard fades out over about 1.5 ms */
            ur.primed = 0;
            a = ur.lastl - (ur.lastl >> 6) - (ur.lastl > 0) + (ur.lastl < 0);
            b = ur.lastr - (ur.lastr >> 6) - (ur.lastr > 0) + (ur.lastr < 0);
        }
        ur.lastl = a;
        ur.lastr = b;
        g = clamp(g + dg, 0, 32767);
        l[i] = (a * g) >> 15;
        r[i] = (b * g) >> 15;
    }
    ur.g = g;
    if (!sys.usbrec && !g)
        ur.isr_in = 0;                                  /* faded out: the tracks render again from the next block */
}

/* ------------------------------------------------------- main loop --- */
/* chunks the take could still have: free ones, and cleared and parked tapes' (what tape_alloc(.., 0) takes) */
static uint32_t ur_room(void)
{
    uint32_t t, n = mem_count(MEM_FREE);
    for (t = 0; t < NTRK; t++)
        if (tape_ctl[t].nch && !tape_ctl[t].grow && (tape_ctl[t].empty || tape_parked(t)))
            n += tape_ctl[t].nch;
    n += ur.nch;
    return n > TAPE_MAXCH ? TAPE_MAXCH : n;
}

/* the take's chunks back to the pool */
static void ur_free(void)
{
    while (ur.nch)
        mem_free(ur.map[--ur.nch]);
    ur.nblk = ur.bn = ur.pk = 0;
    ur.pred = ur.idx = 0;
    ur.cut = 0;
}

/* the take's block b */
static inline mem_chunk_t *ur_chunk(uint32_t b) { return mem_at(ur.map[b / MEM_CB]); }

/* GAIN as Q12: dB (db_q10), or NORM: the loudest block in the kept part to full scale (at most +24 dB) */
static int32_t ur_gain_q12(void)
{
    uint32_t b, pk = 0;
    if (ur.kv[UK_GAIN] != UR_NORM)
        return db_q10(clamp(ur.kv[UK_GAIN], -24, 24)) * 4;
    for (b = (uint32_t)ur.kv[UK_STRT]; b < (uint32_t)(ur.kv[UK_STRT] + ur.kv[UK_LEN]) && b < ur.nblk; b++) {
        uint32_t p = ur_chunk(b)->peak[b % MEM_CB];
        pk = p > pk ? p : pk;
    }
    pk = pk * 128u + 127u;                              /* (a block's peak is kept >> 7) */
    return (int32_t)(32767u * 4096u / pk > 65536u ? 65536u : 32767u * 4096u / pk);
}

/* CHOOSE's knobs into what the preview plays and keeping keeps */
static void ur_trim(void)
{
    int32_t n = (int32_t)ur.nblk;
    ur.kv[UK_STRT] = (int16_t)clamp(ur.kv[UK_STRT], 0, n > 0 ? n - 1 : 0);
    ur.kv[UK_LEN] = (int16_t)clamp(ur.kv[UK_LEN], 1, n - ur.kv[UK_STRT] > 1 ? n - ur.kv[UK_STRT] : 1);
    ur.kv[UK_GAIN] = (int16_t)clamp(ur.kv[UK_GAIN], -24, UR_NORM);
    ur.kv[UK_FADE] = (int16_t)clamp(ur.kv[UK_FADE], 0, 100);
    ur.pv_g = ur_gain_q12();
    ur.pv_f = (uint32_t)ur.kv[UK_FADE] * UR_FADE_MS * (TAPE_SR / 1000u);
    ur.pv_e = (uint32_t)(ur.kv[UK_STRT] + ur.kv[UK_LEN]) * TAPE_BLK;
    RING_PUBLISH();
    ur.pv_s = (uint32_t)ur.kv[UK_STRT] * TAPE_BLK;
}

/* KNOB k turned d detents in CHOOSE: START and LENGTH a block (11.6 ms) a detent, faster when turned fast */
static void usbrec_knob(uint32_t k, int32_t d)
{
    int32_t a = d < 0 ? -d : d, m = k <= UK_LEN ? (a >= 4 ? 16 : a >= 2 ? 4 : 1) : 1;
    if (ur.state != UR_CHOOSE || k > UK_FADE)
        return;
    ur.kv[k] = (int16_t)(ur.kv[k] + d * m);
    ur_trim();
}

/* chunks the kept take takes, and what would be free once it's kept (the rest of the take, and the track's old
 * tape, given back) */
static uint32_t ur_keep_chunks(void) { return ((uint32_t)ur.kv[UK_LEN] + MEM_CB - 1u) / MEM_CB; }
static uint32_t ur_free_after(void)
{
    uint32_t n = mem_count(MEM_FREE) + ur.nch - ur_keep_chunks();
    if (ur.dest >= 0)
        n += tape_ctl[(uint32_t)ur.dest].nch;
    return n;
}

/* main loop, keeping: the trimmed, adjusted take at the start of its chunks, the chunks past it given back */
static void ur_shape(void)
{
    uint32_t s = (uint32_t)ur.kv[UK_STRT], n = (uint32_t)ur.kv[UK_LEN], total = n * TAPE_BLK, k, i;
    uint32_t fn = ur.pv_f;
    int32_t g = ur.pv_g;
    for (k = 0; k < n; k++) {
        uint32_t b = s + k, j0 = k * TAPE_BLK;
        mem_chunk_t *src = ur_chunk(b), *dst = ur_chunk(k);
        uint32_t so = b % MEM_CB, d = k % MEM_CB;
        int fade = fn && (j0 < fn || total - (j0 + TAPE_BLK) < fn);
        if (g == 4096 && !fade) {                       /* untouched: its bits as they are */
            if (b != k) {
                memcpy(dst->data[d], src->data[so], sizeof dst->data[d]);
                dst->pred[d] = src->pred[so];
                dst->idx[d] = src->idx[so];
                dst->peak[d] = src->peak[so];
            }
        } else {                                        /* decoded, shaped, encoded again */
            int16_t x[TAPE_BLK];
            int32_t pred, idx, pk = 0;
            ima_dec(src->data[so], src->pred[so], src->idx[so], x, TAPE_BLK);
            idx = src->idx[so];
            for (i = 0; i < TAPE_BLK; i++) {
                uint32_t j = j0 + i, a = j < total - 1u - j ? j : total - 1u - j;
                int32_t y = (x[i] * g) >> 12;
                if (fn && a < fn)
                    y = y * (int32_t)a / (int32_t)fn;
                y = clamp(y, -32767, 32767);
                x[i] = (int16_t)y;
                pk = (y < 0 ? -y : y) > pk ? (y < 0 ? -y : y) : pk;
            }
            pred = x[0];
            dst->pred[d] = (int16_t)pred;
            dst->idx[d] = (uint8_t)idx;
            ima_enc_st(x, &pred, &idx, dst->data[d], TAPE_BLK);
            dst->peak[d] = (uint8_t)((uint32_t)pk >> 7 > 255u ? 255u : (uint32_t)pk >> 7);
        }
    }
    ur.nblk = n;
    while (ur.nch > ur_keep_chunks())
        mem_free(ur.map[--ur.nch]);
}

/* one sample of the take into its blocks; 0 when there's no room for it (the take is full) */
static int ur_emit(int32_t x)
{
    uint32_t a = (uint32_t)(x < 0 ? -x : x);
    ur.blk[ur.bn++] = (int16_t)x;
    if (a > ur.pk)
        ur.pk = a;
    if (ur.bn == TAPE_BLK) {
        mem_chunk_t *m;
        uint32_t o = ur.nblk % MEM_CB;
        if (ur.nblk / MEM_CB >= ur.nch) {               /* a new chunk */
            int32_t k = ur.nch < TAPE_MAXCH ? tape_alloc(MEM_IMPORT, NTRK, 0) : -1;
            if (k < 0) {
                ur.bn--;
                return 0;
            }
            ur.map[ur.nch++] = (uint8_t)k;
        }
        m = mem_at(ur.map[ur.nblk / MEM_CB]);
        m->pred[o] = (int16_t)ur.pred;
        m->idx[o] = (uint8_t)ur.idx;
        ima_enc_st(ur.blk, &ur.pred, &ur.idx, m->data[o], TAPE_BLK);
        m->peak[o] = (uint8_t)(ur.pk >> 7 > 255u ? 255u : ur.pk >> 7);
        ur.nblk++;
        ur.bn = 0;
        ur.pk = 0;
    }
    return 1;
}

static void ur_stop(void)                               /* the take ends: what's in the ring, then the last block */
{
    ur.feed = 0;
    RING_PUBLISH();
    while (ur_rr != ur_rw && !ur.cut) {
        if (!ur_emit(ur_rec[ur_rr % UR_REC]))
            ur.cut = 1;
        ur_rr++;
    }
    ur_rr = ur_rw;
    if (ur.bn && !ur.cut)                               /* (the part block: filled out with silence) */
        while (ur.bn && ur_emit(0))
            ;
    ur.dest = -1;
    if (!ur.nblk) {
        ur_free();
        ur.state = UR_READY;
        return;
    }
    ur.kv[UK_STRT] = 0;                                 /* (the whole take, as it came, a 10 ms fade at the ends) */
    ur.kv[UK_LEN] = (int16_t)ur.nblk;
    ur.kv[UK_GAIN] = 0;
    ur.kv[UK_FADE] = 2;
    ur_trim();
    ur.take_gen++;
    RING_PUBLISH();
    ur.state = UR_CHOOSE;
}

/* REC held a second: the mode opens */
static void usbrec_enter(void)
{
    uint32_t t;
    if (sys.usbrec || ur.state != UR_OFF || ur.isr_in)  /* (the last time's monitor still fading out) */
        return;
    sys.playing = 0;
    for (t = 0; t < NTRK; t++)
        if ((sys.rec >> t) & 1u)
            tape_unprepare(t);
    sys.rec = 0;
    ur_mw = ur_mr = ur_rw = ur_rr = 0;
    ur.feed = 0;
    ur.odd = 0;
    ur.primed = 0;
    ur.pos = 0;
    ur.g = 0;
    ur.lastl = ur.lastr = 0;
    ur.peak = 0;
    ur.lost = 0;
    ur.frames = 0;
    ur.seen_frames = 0;
    ur.seen_ms = fm1_ms - 1000u;
    ur_free();
    RING_PUBLISH();
    ur.state = UR_READY;                                /* (TIMER5 starts writing the monitor's ring) */
    sys.usbrec = 1;                                     /* (the ISR fades the tracks out and plays the monitor) */
}

/* the mode closes (the take, if any, already placed or thrown away) */
static void usbrec_leave(void)
{
    if (ur.state == UR_OFF)
        return;
    ur.feed = 0;
    ur_free();
    sys.usbrec = 0;                                     /* (the ISR fades the monitor out, then the tracks) */
    RING_PUBLISH();
    ur.state = UR_OFF;
}

/* REC in the mode */
static void usbrec_rec(void)
{
    switch (ur.state) {
    case UR_READY:
        if (ur_room() < 1u) {
            ui_message("NO MEMORY FREE TO RECORD");
            break;
        }
        ur_free();
        ur_rr = ur_rw;
        ur.odd = 0;
        RING_PUBLISH();
        ur.feed = 1;
        ur.state = UR_RECORDING;
        break;
    case UR_RECORDING:
        ur_stop();
        break;
    case UR_CHOOSE: {
        char m[40] = "TRACK ";
        uint32_t t = (uint32_t)ur.dest;
        if (ur.dest < 0) {
            ui_message("PICK A TRACK: WHITE KEYS 1-4");
            break;
        }
        ur.state = UR_KEEPING;                          /* (the preview stops reading the take) */
        RING_PUBLISH();
        ur_shape();
        tape_replace(t, ur.map, ur.nch, ur.nblk);
        ur.nch = 0;                                     /* (the chunks are the tape's now) */
        sys.sel = (uint8_t)t;
        fmt_int(m + 6, (int32_t)t + 1);
        str_cpy(m + str_len(m), "'S TAPE: THE USB TAKE", 22);
        usbrec_leave();
        ui_message(m);
        break;
    }
    default:
        break;
    }
}

/* HOME in the mode: back a step (a take thrown away; from READY, out) */
static void usbrec_back(void)
{
    if (ur.state == UR_RECORDING) {
        ur.feed = 0;
        ur_rr = ur_rw;
        ur_free();
        ur.state = UR_READY;
        ui_message("TAKE THROWN AWAY");
    } else if (ur.state == UR_CHOOSE) {
        ur.state = UR_READY;                            /* (the preview stops reading it first) */
        RING_PUBLISH();
        ur_free();
        ui_message("TAKE THROWN AWAY");
    } else {
        usbrec_leave();
    }
}

/* a white key in the mode: CHOOSE's track */
static void usbrec_pick(uint32_t k)
{
    if (ur.state == UR_CHOOSE && k < sys.ntrk)
        ur.dest = (int8_t)k;
}

/* main loop, every pass: the take's ring into its blocks; full, it stops */
static void usbrec_poll(void)
{
    uint32_t n = 0;
    if (ur.frames != ur.seen_frames) {
        ur.seen_frames = ur.frames;
        ur.seen_ms = fm1_ms;
    }
    if (ur.state != UR_RECORDING)
        return;
    while (ur_rr != ur_rw && n++ < 2048u) {             /* (at most 93 ms of it a pass: the screen keeps up) */
        if (!ur_emit(ur_rec[ur_rr % UR_REC])) {
            ur.cut = 1;
            break;
        }
        ur_rr++;
    }
    if (ur.cut) {
        ur_stop();
        ui_message("THE TAKE STOPPED: MEMORY FULL");
    }
}

/* the screen: is the computer sending sound (frames in the last 200 ms)? */
static int usbrec_live(void) { return ur.frames != ur.seen_frames || (uint32_t)(fm1_ms - ur.seen_ms) < 200u; }

/* the take so far, tenths of a second */
static uint32_t usbrec_tenths(void) { return (ur.nblk * TAPE_BLK + ur.bn + (ur_rw - ur_rr)) * 10u / TAPE_SR; }

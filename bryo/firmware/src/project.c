/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: projects (phase 8). A project is the whole instrument's state: every track's knobs on every page, its source,
 * its modulator slots and their depths, its SEQ steps, its DRUM pattern, the mixer, the tempo, TRACKS, and what each
 * tape plays. SAVE stores it; a power cycle brings back the last one saved.
 *
 * Where: six slots, each an A/B pair of 4 KiB sectors in Felucca's old data region (its project and preset areas;
 * Bryo's user reels have the sample area). A save goes to the copy that isn't the current one: erase it, program the
 * payload, then the commit record (magic, seq, lengths, CRCs) LAST. Loading takes the valid copy with the newest seq,
 * so a save torn by a power cut leaves the previous one in charge. The record's magic is Bryo's own: a reinstalled
 * Felucca sees an empty sector, never a project of the wrong shape.
 *
 * What: a byte stream, written and read field by field (no struct is dumped, so the layout of the RAM can change):
 * a version, then every array as a count and its values. A newer Bryo that adds knobs reads an older project as far
 * as it goes and leaves the rest at their defaults; every value is clamped to its knob's range as it's read. The
 * modulation depths are stored sparsely, by array and knob (not by target number, which moved when DRUM came in).
 * The stream is packed (PackBits: runs of a byte, literals otherwise), written and read a byte at a time straight to
 * and from the flash, so no RAM holds a whole project: a project is about 2 KB packed, 3.8 KB at the very most, and
 * a sector's payload holds 3,840 B.
 *
 * The tapes: a track playing a factory reel or one of your reels stores which. A track playing its own RAM tape with
 * something on it can't keep that over a power cycle, so SAVE copies the tape into a user reel named after the
 * project and the track ("P2T3"; the same reel again on the next save if it still fits, else the first free run of
 * slots), switches the track to that reel (the sound is the same; the RAM goes back to the pool) and stores that.
 * With no reel free, the project is saved without that tape, and SAVE says so.
 *
 * Main loop only. Loading stops the transport, lets REC go and clears the undo buffers (they belonged to another
 * session); the ISR picks the knobs up block by block as it always does, and the depth lists are rebuilt and
 * published whole (mod.c). */

#define PJ_N 6u                      /* project slots */
#define PJ_MAGIC 0x50595242u         /* "BRYP" */
#define PJ_VERSION 1u
#define PJ_SECTOR 4096u
#define PJ_PAYLOAD 256u              /* the payload starts after the commit record's page */
#define PJ_PAYLOAD_MAX (PJ_SECTOR - PJ_PAYLOAD)

/* the sectors: slot s, copy c (0 A, 1 B). Slots 1-4 sit where Felucca kept its four projects, slot 5 over its FM6
 * bank and the first of its preset sectors, slot 6 in the rest of the preset area (0xDF000 is left over) */
static uint32_t pj_sector(uint32_t s, uint32_t c)
{
    static const uint32_t SEC[PJ_N][2] = {{0x97000u, 0x98000u}, {0x99000u, 0x9A000u}, {0x9B000u, 0x9C000u},
                                          {0x9D000u, 0x9E000u}, {0x9F000u, 0xDC000u}, {0xDD000u, 0xDE000u}};
    return SEC[s % PJ_N][c & 1u];
}

typedef struct {                     /* the commit record, at the sector's start, programmed last */
    uint32_t magic;
    uint16_t slot, copy;
    uint32_t seq;
    uint32_t len;                    /* the payload, packed */
    uint32_t crc;                    /* .. its CRC-32 */
    uint32_t raw;                    /* .. unpacked */
    uint32_t hcrc;                   /* the record's own CRC (all of it before this) */
} pj_hdr_t;

/* what the project view shows of a slot without loading it */
typedef struct {
    uint8_t used;
    uint8_t src[NTRK];               /* each track's source (SRC_*) */
    uint16_t bpm;
    uint32_t seq;                    /* the newest copy's (the last one saved has the highest) */
} pj_sum_t;

static pj_sum_t pj_sum[PJ_N];
static uint8_t pj_cur;               /* the project loaded or saved last (SAVE tapped saves into it) */
static uint32_t pj_hash_at;          /* the state's fingerprint when it was loaded or saved: changed since? */
static uint32_t pj_seq;              /* the newest seq of all (a save takes the next) */

/* --------------------------------------------------------------------------- the commit record --- */
static uint32_t pj_crc(const void *p, uint32_t n) { return ~rf_crc_upd(0xFFFFFFFFu, p, n); }

/* copy c of slot s's record, valid: 0 */
static int pj_head(uint32_t s, uint32_t c, pj_hdr_t *h)
{
    const uint8_t *p = rf_ptr(pj_sector(s, c));
    uint32_t i;
    for (i = 0; i < sizeof *h; i++)
        ((uint8_t *)h)[i] = p[i];
    if (h->magic != PJ_MAGIC || h->slot != s || h->copy != c || h->len > PJ_PAYLOAD_MAX ||
        h->hcrc != pj_crc(h, sizeof *h - 4u))
        return -1;
    return pj_crc(rf_ptr(pj_sector(s, c) + PJ_PAYLOAD), h->len) == h->crc ? 0 : -1;
}

/* the copy of slot s that's in charge (the newest valid one), or -1: none */
static int pj_current(uint32_t s, pj_hdr_t *h)
{
    pj_hdr_t a, b;
    int va = pj_head(s, 0, &a) == 0, vb = pj_head(s, 1, &b) == 0;
    if (vb && (!va || (b.seq != a.seq && b.seq - a.seq < 0x80000000u))) {
        *h = b;
        return 1;
    }
    if (va) {
        *h = a;
        return 0;
    }
    return -1;
}

/* ------------------------------------------------------------------------------- the writer --- */
/* PackBits as the bytes come: a header n 0..127 is n + 1 bytes as they are, 129..255 (n - 256 = -127..-1) the next
 * byte 1 - (n - 256) times. A run is kept apart from 3 bytes on; the worst case grows by a byte in 128. The packed
 * bytes go to the flash a 256-byte page at a time, into the CRC as they go */
static struct {
    uint32_t base, off, crc, raw;
    int err;
    uint8_t page[256];
    uint32_t np;
    uint8_t lit[128];
    uint32_t nl, rn;
    uint8_t rb;
} pw;

static void pw_out(uint8_t b)
{
    if (pw.err)
        return;
    if (pw.off + pw.np >= PJ_PAYLOAD_MAX) {
        pw.err = -2;                                     /* (can't happen: 3.8 KB is the most a project packs to) */
        return;
    }
    pw.page[pw.np++] = b;
    if (pw.np == sizeof pw.page) {
        pw.crc = rf_crc_upd(pw.crc, pw.page, pw.np);
        if (rf_prog(pw.base + PJ_PAYLOAD + pw.off, pw.page, pw.np))
            pw.err = -1;
        pw.off += pw.np;
        pw.np = 0;
    }
}
static void pw_lit_flush(void)
{
    uint32_t i;
    if (!pw.nl)
        return;
    pw_out((uint8_t)(pw.nl - 1u));
    for (i = 0; i < pw.nl; i++)
        pw_out(pw.lit[i]);
    pw.nl = 0;
}
static void pw_lit(uint8_t b)
{
    pw.lit[pw.nl++] = b;
    if (pw.nl == sizeof pw.lit)
        pw_lit_flush();
}
static void pw_run_close(void)
{
    if (pw.rn >= 3u) {
        pw_lit_flush();
        pw_out((uint8_t)(257u - pw.rn));
        pw_out(pw.rb);
    } else {
        while (pw.rn--)
            pw_lit(pw.rb);
    }
    pw.rn = 0;
}
static void pw_byte(uint8_t b)
{
    pw.raw++;
    if (pw.rn && b == pw.rb && pw.rn < 128u) {
        pw.rn++;
        return;
    }
    if (pw.rn)
        pw_run_close();
    pw.rb = b;
    pw.rn = 1;
}
static void pw_u16(uint32_t v)
{
    pw_byte((uint8_t)v);
    pw_byte((uint8_t)(v >> 8));
}
static void pw_a16(const int16_t *a, uint32_t n)        /* an array: its count, its values */
{
    uint32_t i;
    pw_u16(n);
    for (i = 0; i < n; i++)
        pw_u16((uint16_t)a[i]);
}
static void pw_a8(const int8_t *a, uint32_t n)
{
    uint32_t i;
    pw_u16(n);
    for (i = 0; i < n; i++)
        pw_byte((uint8_t)a[i]);
}
/* the last page out, the length: 0, or what failed */
static int pw_finish(void)
{
    if (pw.rn)
        pw_run_close();
    pw_lit_flush();
    if (!pw.err && pw.np) {
        pw.crc = rf_crc_upd(pw.crc, pw.page, pw.np);
        if (rf_prog(pw.base + PJ_PAYLOAD + pw.off, pw.page, pw.np))
            pw.err = -1;
        pw.off += pw.np;
        pw.np = 0;
    }
    return pw.err;
}

/* ------------------------------------------------------------------------------- the reader --- */
static struct {
    const uint8_t *src;
    uint32_t len, at;                /* packed bytes, read so far */
    int32_t lit, run;                /* bytes left of the literal or the run under way */
    uint8_t rb;
    int bad;                         /* ran off the end: what's read after it is zero */
} pr;

static uint8_t pr_byte(void)
{
    while (!pr.lit && !pr.run) {
        int32_t h;
        if (pr.at >= pr.len) {
            pr.bad = 1;
            return 0;
        }
        h = (int8_t)pr.src[pr.at++];
        if (h >= 0)
            pr.lit = h + 1;
        else if (h != -128) {
            if (pr.at >= pr.len) {
                pr.bad = 1;
                return 0;
            }
            pr.rb = pr.src[pr.at++];
            pr.run = 1 - h;
        }
    }
    if (pr.run) {
        pr.run--;
        return pr.rb;
    }
    pr.lit--;
    if (pr.at >= pr.len) {
        pr.bad = 1;
        return 0;
    }
    return pr.src[pr.at++];
}
static uint32_t pr_u16(void)
{
    uint32_t lo = pr_byte();
    return lo | (uint32_t)pr_byte() << 8;
}
/* an array into a of nmax, clamped by d (a descriptor per value, 0: as read): as many as were saved and fit; the
 * ones a newer project has past nmax are read and dropped */
static void pr_a16(int16_t *a, uint32_t nmax, const pdesc_t *d)
{
    uint32_t n = pr_u16(), i;
    for (i = 0; i < n && !pr.bad; i++) {
        int16_t v = (int16_t)pr_u16();
        if (i >= nmax)
            continue;
        if (d)
            v = pdesc_empty(&d[i]) ? 0 : (int16_t)clamp(v, d[i].min, d[i].max);
        a[i] = v;
    }
}
static void pr_a8(int8_t *a, uint32_t nmax, int32_t lo, int32_t hi)
{
    uint32_t n = pr_u16(), i;
    for (i = 0; i < n && !pr.bad; i++) {
        int8_t v = (int8_t)pr_byte();
        if (i < nmax)
            a[i] = (int8_t)clamp(v, lo, hi);
    }
}

/* -------------------------------------------------------------------------------- the state --- */
static int pj_own_tape(uint32_t t);

/* a fingerprint of everything a project holds (has anything changed since it was loaded or saved?) */
static uint32_t pj_hash(void)
{
    uint32_t h = 2166136261u, t, i;
    const uint8_t *p;
    for (p = (const uint8_t *)tp, i = 0; i < sizeof tp; i++)
        h = (h ^ p[i]) * 16777619u;
    for (p = (const uint8_t *)mdep, i = 0; i < sizeof mdep; i++)
        h = (h ^ p[i]) * 16777619u;
    for (t = 0; t < NTRK; t++) {
        h = (h ^ (track[t].level | track[t].mute << 8 | track[t].octave << 16)) * 16777619u;
        if (pj_own_tape(t))                              /* (its own tape: what's recorded on it) */
            h = (h ^ tape_ver[t]) * 16777619u;
    }
    for (i = 0; i < NMS; i++)
        h = (h ^ (uint32_t)(uint16_t)mst[i]) * 16777619u;
    return (h ^ (sys.bpm | (uint32_t)sys.ntrk << 16)) * 16777619u;
}

/* the whole state into the stream (the writer set up) */
static void pj_write_state(void)
{
    uint32_t t, s, b, n = 0, g;
    pw_byte('B');
    pw_byte('P');
    pw_byte(PJ_VERSION);
    pw_u16(sys.bpm);
    pw_byte(sys.ntrk);
    pw_a16(mst, NMS);
    pw_byte(NTRK);
    for (t = 0; t < NTRK; t++) {
        const track_params_t *P = &tp[t];
        pw_byte(track[t].level);
        pw_byte(track[t].mute);
        pw_byte(track[t].octave);
        pw_byte(P->src);
        pw_byte(P->recin);
        pw_u16(NDEV);
        for (s = 0; s < NDEV; s++)
            pw_a16(P->dev[s], NPK);
        pw_u16(NSLOT);
        for (s = 0; s < NSLOT; s++) {
            pw_byte(P->engine[s]);
            pw_a16(P->mod[s], NPK);
            pw_a8(P->steps[s], 16);
        }
        pw_a16(P->ch, NCH);
        pw_a16(P->syn, NPK);
        pw_a16(P->pol, NPK);
        pw_a16(P->drm, NPK);
        pw_u16(DRM_NBAR);
        for (b = 0; b < DRM_NBAR; b++)
            pw_a16((const int16_t *)P->dpat[b], 16);
        pw_a16((const int16_t *)P->dacc, DRM_NBAR);
        pw_u16(DRM_NINST);
        for (s = 0; s < DRM_NINST; s++)
            pw_a16(P->dins[s], NDIN);
    }
    for (t = 0; t < NTRK; t++)                           /* the depths, sparse: track, slot, array, knob, depth */
        for (s = 0; s < NSLOT; s++)
            for (g = 0; g < MOD_NTGT; g++)
                n += mdep[t][s][g] != 0;
    pw_u16(n);
    for (t = 0; t < NTRK; t++)
        for (s = 0; s < NSLOT; s++)
            for (g = 0; g < MOD_NTGT; g++)
                if (mdep[t][s][g]) {
                    uint32_t k, a = mod_tarr(g, &k);
                    pw_byte((uint8_t)t);
                    pw_byte((uint8_t)s);
                    pw_byte((uint8_t)a);
                    pw_byte((uint8_t)k);
                    pw_byte((uint8_t)mdep[t][s][g]);
                }
}

/* knob k of array a (MA_* / DEV_*) as a modulation target, or MOD_NTGT */
static uint32_t pj_target(uint32_t a, uint32_t k)
{
    if (a >= 1u && a < NDEV && k < NPK)
        return MOD_TG(a, k);
    if (k >= NPK)
        return MOD_NTGT;
    switch (a) {
    case DEV_SRC: return MOD_TSRC + SRC_TAPE * NPK + k;
    case MA_SYN: return MOD_TSRC + SRC_SYNTH * NPK + k;
    case MA_POL: return MOD_TSRC + SRC_POLY * NPK + k;
    case MA_DRM: return MOD_TSRC + SRC_DRUM * NPK + k;
    case MA_CH: return k < NCH ? MOD_TCH + k : MOD_NTGT;
    default: return MOD_NTGT;
    }
}

/* the stream (the reader set up, its CRC already checked) into the state: 0, or -1 when it isn't one this Bryo reads
 * (refused before anything changes: the version and the track count come first) */
static int pj_read_state(void)
{
    uint32_t t, s, b, n, i, nt;
    if (pr_byte() != 'B' || pr_byte() != 'P' || pr_byte() != PJ_VERSION || pr.bad)
        return -1;
    {                                                    /* (the track count, looked at before anything changes) */
        uint32_t at = pr.at, lit = (uint32_t)pr.lit, run = (uint32_t)pr.run, nt;
        uint8_t rb = pr.rb;
        int16_t junk[NMS];
        (void)pr_u16();
        (void)pr_byte();
        pr_a16(junk, NMS, 0);
        nt = pr_byte();
        pr.at = at, pr.lit = (int32_t)lit, pr.run = (int32_t)run, pr.rb = rb;
        if (nt > NTRK || pr.bad)
            return -1;
    }
    param_defaults();                                    /* (what an older project doesn't have: as at power-on) */
    for (t = 0; t < NTRK; t++)
        for (s = 0; s < NSLOT; s++)
            for (i = 0; i < MOD_NTGT; i++)
                mdep[t][s][i] = 0;
    sys.bpm = (uint16_t)clamp((int32_t)pr_u16(), 40, 240);
    sys.ntrk = (uint8_t)clamp(pr_byte(), 1, (int32_t)NTRK);
    pr_a16(mst, NMS, MS_P);
    nt = pr_byte();
    if (nt > NTRK)                                       /* (more tracks than this FM-1 has: not ours to read) */
        return -1;
    for (t = 0; t < nt && !pr.bad; t++) {
        track_params_t *P = &tp[t];
        uint32_t nd, ns, nb, ni;
        uint8_t lv = pr_byte(), mu = pr_byte(), oc = pr_byte(), src = pr_byte(), rin = pr_byte();
        track[t].level = (uint8_t)clamp(lv, 0, 127);
        track[t].mute = (uint8_t)(mu != 0);
        track[t].octave = (uint8_t)clamp(oc, 1, 6);
        P->src = (uint8_t)(src < NSRC ? src : SRC_TAPE);
        P->recin = (uint8_t)(rin < NRIN ? rin : RIN_AUTO);
        nd = pr_u16();
        for (s = 0; s < nd && !pr.bad; s++) {
            int16_t junk[NPK];
            if (s < NDEV)
                pr_a16(P->dev[s], NPK, DEV_P[s]);
            else
                pr_a16(junk, 0, 0);
        }
        ns = pr_u16();
        for (s = 0; s < ns && !pr.bad; s++) {
            int16_t junk[NPK];
            int8_t junk8[16];
            uint8_t e = pr_byte();
            if (s < NSLOT) {
                P->engine[s] = (uint8_t)(e < NME ? e : ME_WAVE);
                pr_a16(P->mod[s], NPK, ME_P[P->engine[s]]);
                pr_a8(P->steps[s], 16, 0, 100);
            } else {
                pr_a16(junk, 0, 0);
                pr_a8(junk8, 0, 0, 0);
            }
        }
        pr_a16(P->ch, NCH, CH_P);
        pr_a16(P->syn, NPK, SYN_P);
        pr_a16(P->pol, NPK, POL_P);
        pr_a16(P->drm, NPK, DRM_P);
        nb = pr_u16();
        for (b = 0; b < nb && !pr.bad; b++) {
            int16_t junk[16];
            pr_a16(b < DRM_NBAR ? (int16_t *)P->dpat[b] : junk, b < DRM_NBAR ? 16u : 0u, 0);
        }
        pr_a16((int16_t *)P->dacc, DRM_NBAR, 0);
        ni = pr_u16();
        for (s = 0; s < ni && !pr.bad; s++) {
            int16_t junk[NDIN];
            pr_a16(s < DRM_NINST ? P->dins[s] : junk, s < DRM_NINST ? NDIN : 0u, DRI_P);
        }
    }
    n = pr_u16();
    for (i = 0; i < n && !pr.bad; i++) {
        uint32_t tt = pr_byte(), ss = pr_byte(), a = pr_byte(), k = pr_byte(), g = pj_target(a, k);
        int32_t dp = (int8_t)pr_byte();
        if (tt < NTRK && ss < NSLOT && g < MOD_NTGT && mod_tdesc(g))
            mdep[tt][ss][g] = (int8_t)clamp(dp, -100, 100);
    }
    return 0;
}

/* --------------------------------------------------------------------------------- the slots --- */
/* slot s's summary from its current copy (the view's list) */
static void pj_scan(uint32_t s)
{
    pj_hdr_t h;
    pj_sum_t *m = &pj_sum[s % PJ_N];
    uint32_t t;
    m->used = 0;
    if (pj_current(s, &h) < 0)
        return;
    m->seq = h.seq;
    if (h.seq - pj_seq < 0x80000000u)
        pj_seq = h.seq;
    pr.src = rf_ptr(pj_sector(s, (uint32_t)pj_current(s, &h)) + PJ_PAYLOAD);
    pr.len = h.len;
    pr.at = 0;
    pr.lit = pr.run = 0;
    pr.bad = 0;
    if (pr_byte() != 'B' || pr_byte() != 'P' || pr_byte() != PJ_VERSION)
        return;
    m->bpm = (uint16_t)pr_u16();
    (void)pr_byte();
    {
        int16_t junk[NMS];
        pr_a16(junk, NMS, 0);
    }
    (void)pr_byte();
    for (t = 0; t < NTRK; t++) {                         /* (each track's source: the first fields, then skip to the
                                                          * next track by reading through) */
        uint32_t s2, k;
        int16_t junk[NPK];
        int8_t junk8[16];
        (void)pr_byte();
        (void)pr_byte();
        (void)pr_byte();
        m->src[t] = pr_byte();
        (void)pr_byte();
        for (k = pr_u16(), s2 = 0; s2 < k; s2++)
            pr_a16(junk, NPK, 0);
        for (k = pr_u16(), s2 = 0; s2 < k; s2++) {
            (void)pr_byte();
            pr_a16(junk, NPK, 0);
            pr_a8(junk8, 16, -128, 127);
        }
        for (s2 = 0; s2 < 4u; s2++)
            pr_a16(junk, NPK, 0);
        for (k = pr_u16(), s2 = 0; s2 < k; s2++)
            pr_a16(junk, 16, 0);
        pr_a16(junk, NPK, 0);
        for (k = pr_u16(), s2 = 0; s2 < k; s2++)
            pr_a16(junk, NPK, 0);
    }
    m->used = !pr.bad;
}

/* main loop, at power-on: every slot's summary, then the project saved last, loaded (none: the defaults stay) */
static int pj_load(uint32_t s);
static void pj_boot(void)
{
    uint32_t s, best = PJ_N, bseq = 0;
    pj_seq = 0;
    for (s = 0; s < PJ_N; s++) {
        pj_scan(s);
        if (pj_sum[s].used && (best == PJ_N || pj_sum[s].seq - bseq < 0x80000000u)) {
            best = s;
            bseq = pj_sum[s].seq;
        }
    }
    pj_cur = 0;
    if (best < PJ_N)
        pj_load(best);
    else
        pj_hash_at = pj_hash();
}

/* the user reel slot that holds project s's track t ("P2T3"), or -1 */
static int32_t pj_reel_of(uint32_t s, uint32_t t)
{
    uint32_t k;
    for (k = 0; k < USLOT_N; k++)
        if (uslot_valid(k)) {
            const char *n = uslot_hdr(k)->name;
            if (n[0] == 'P' && n[1] == (char)('1' + s) && n[2] == 'T' && n[3] == (char)('1' + t) && !n[4])
                return (int32_t)k;
        }
    return -1;
}

/* track t plays its own RAM tape: TAPE on it (REEL 0), or POLY reading it (its REEL 0) */
static int pj_own_tape(uint32_t t) { return tp[t].dev[DEV_SRC][TK_REEL] == 0 || tp[t].pol[PL_REEL] == 0; }

/* main loop: track t's own tape into a user reel for project s, what played it switched to the reel. 1: done, 0:
 * nothing to keep, -1: no reel free for it, -2: the flash refused */
static int pj_keep_tape(uint32_t s, uint32_t t)
{
    tape_view_t v;
    uint32_t nblk, need;
    int32_t at, own = pj_reel_of(s, t);
    char name[5] = {'P', (char)('1' + s), 'T', (char)('1' + t), 0};
    if (!pj_own_tape(t))
        return 0;
    tape_view_of(t, 0, &v);
    nblk = v.len / TAPE_BLK;
    if (!nblk)
        return 0;
    need = uslot_span(nblk);
    at = -1;
    if (own >= 0) {                                      /* its reel from the last save, if it still fits there */
        uint32_t sp = uslot_hdr((uint32_t)own)->span ? uslot_hdr((uint32_t)own)->span : 1u;
        if (sp + uslot_run_at((uint32_t)own + sp) >= need)
            at = own;
    }
    if (at < 0)
        at = uslot_free_run(need);
    if (at < 0)
        return -1;
    if (uslot_save_view((uint32_t)at, &v, nblk, name) != (int32_t)nblk)
        return -2;
    if (tp[t].dev[DEV_SRC][TK_REEL] == 0)               /* what played the tape plays the reel now: the same sound */
        tp[t].dev[DEV_SRC][TK_REEL] = (int16_t)(NREEL + 1u + (uint32_t)at);
    if (tp[t].pol[PL_REEL] == 0)
        tp[t].pol[PL_REEL] = (int16_t)(NREEL + 1u + (uint32_t)at);
    tape_ctl[t].empty = 1;                                /* (its RAM goes back to the pool as memory is needed) */
    tape_ver[t]++;
    return 1;
}

/* main loop: the state into slot s. Returns 0 when everything went in; a negative number when the flash refused
 * (the slot's previous copy is still in charge; tapes already kept in reels stay kept); otherwise a bit per track
 * whose tape found no reel free (the project is saved, that track without its take) */
static int pj_save(uint32_t s)
{
    pj_hdr_t h, cur;
    uint32_t t, copy;
    int c, lost = 0, rc;
    if (s >= PJ_N)
        return -1;
    sys.rec = 0;                                         /* (REC lets go: a take mid-record can't be kept) */
    for (t = 0; t < NTRK; t++) {
        tape_unprepare(t);
        rc = pj_keep_tape(s, t);
        if (rc == -1)
            lost |= 1 << t;
        else if (rc == -2)
            return -3;
    }
    c = pj_current(s, &cur);
    copy = c == 0 ? 1u : 0u;                             /* the other copy */
    memset(&pw, 0, sizeof pw);
    pw.base = pj_sector(s, copy);
    pw.crc = 0xFFFFFFFFu;
    if (rf_erase(pw.base))
        return -1;
    pj_write_state();
    if (pw_finish())
        return -1;
    memset(&h, 0, sizeof h);
    h.magic = PJ_MAGIC;
    h.slot = (uint16_t)s;
    h.copy = (uint16_t)copy;
    if (c >= 0 && cur.seq - pj_seq < 0x80000000u)       /* (newer than the copy in charge, whatever was scanned) */
        pj_seq = cur.seq;
    h.seq = ++pj_seq;
    h.len = pw.off;
    h.crc = ~pw.crc;
    h.raw = pw.raw;
    h.hcrc = pj_crc(&h, sizeof h - 4u);
    if (rf_prog(pw.base, &h, sizeof h) || pj_head(s, copy, &cur))   /* the commit record, last; read back */
        return -2;
    pj_cur = (uint8_t)s;
    pj_scan(s);
    pj_hash_at = pj_hash();
    return lost;
}

/* main loop: slot s into the state. 0, or -1: an empty slot or one this Bryo can't read (nothing changed) */
static int pj_load(uint32_t s)
{
    pj_hdr_t h;
    int c;
    uint32_t t;
    if (s >= PJ_N || (c = pj_current(s, &h)) < 0)
        return -1;
    pr.src = rf_ptr(pj_sector(s, (uint32_t)c) + PJ_PAYLOAD);
    pr.len = h.len;
    pr.at = 0;
    pr.lit = pr.run = 0;
    pr.bad = 0;
    sys.playing = 0;
    sys.rec = 0;
    if (pj_read_state() < 0)
        return -1;
    for (t = 0; t < NTRK; t++) {
        tape_unprepare(t);
        tape_ctl[t].empty = 1;                           /* its own tape: the take of another session goes (what a
                                                          * project keeps is in its reels) */
        tape_ver[t]++;
        drm_dirty[t] = 0;
        mod_rebuild(t);
    }
    chain_tracks(sys.ntrk);
    tape_undo.valid = 0;                                 /* (the undo buffers belonged to the session before) */
    mod_undo_buf.valid = 0;
    drm_undo_buf.valid = 0;
    pj_cur = (uint8_t)s;
    pj_hash_at = pj_hash();
    return 0;
}

/* main loop: slot s erased (both copies). 0, or -1 */
static int pj_delete(uint32_t s)
{
    if (s >= PJ_N || rf_erase(pj_sector(s, 0)) || rf_erase(pj_sector(s, 1)))
        return -1;
    pj_scan(s);
    return 0;
}

/* the state differs from the project as loaded or saved */
static int pj_changed(void) { return pj_hash() != pj_hash_at; }

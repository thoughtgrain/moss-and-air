/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: user reels, your own sounds in flash (docs/bryo-architecture.md, "Files over USB").
 *
 * Six slots in Felucca's user-sample area (0xA0000..0xDBFFF), 40 KiB each, in the tape's own format so a slot
 * plays straight from flash like a factory reel (through the XIP window) and copying it onto a tape is a plain copy:
 *
 *   the header   magic, seq, name, length in blocks, how many slots it spans, a CRC over all of it and the data,
 *                then each block's decoder state and peak (room for at least 288 blocks). It is programmed LAST, its
 *                magic last of all, so it is the commit record.
 *   the data     the ADPCM blocks, from the first 4 KiB boundary after the header
 *
 * A sound up to 3.3 s (288 blocks) fits one slot: a 4 KiB header sector and 36 KiB of data, the layout reels have
 * always had. A longer one runs on into the slots after it (span), its data in one piece through their space, up to
 * all six (21.5 s). The slots it covers read as empty but aren't free; saving into one of them breaks the long reel
 * (its CRC no longer matches, so it reads as empty too, never as garbage).
 *
 * A save erases the slots, programs the data, then the header. A save torn by a power cut leaves no valid header:
 * the slot reads empty, never half old and half new. (A/B copies would halve the slots; a slot is a sound you still
 * have on your computer.) Saves happen in the main loop with the audio running: the flash driver turns interrupts
 * off per 256 bytes, so the sound stutters for the second a save takes, and the screen says SAVING.
 *
 * Without the flash driver (a RAM-only build) the slots are always empty. */

#define USLOT_BASE 0xA0000u
#define USLOT_SIZE 0xA000u                 /* 40 KiB a slot */
#define USLOT_MAGIC 0x4C454552u            /* "REEL" */
#define USLOT_FIX 24u                      /* the header's fixed part (uslot_hdr_t); the block arrays follow */

typedef struct {
    uint32_t magic, seq;
    char name[8];                          /* up to 4 letters shown, 0-terminated */
    uint16_t nblk, span;                   /* blocks; slots it covers (0, as reels saved before spans: 1) */
    uint32_t crc;                          /* over the header from name on (magic, seq, crc as 0) and the data */
} uslot_hdr_t;                             /* then int16_t pred[n], uint8_t idx[n], uint8_t peak[n]: n = uslot_n() */

/* the block arrays' length for nblk blocks: room for a whole one-slot reel at least (the original layout) */
static uint32_t uslot_n(uint32_t nblk) { return nblk > TAPE_NBLK ? nblk : TAPE_NBLK; }
/* where the data starts, from the slot's start */
static uint32_t uslot_doff(uint32_t nblk) { return (USLOT_FIX + 4u * uslot_n(nblk) + 4095u) & ~4095u; }
/* slots nblk blocks take */
static uint32_t uslot_span(uint32_t nblk) { return (uslot_doff(nblk) + nblk * (TAPE_BLK / 2u) + USLOT_SIZE - 1u) / USLOT_SIZE; }
/* the most blocks k slots hold */
static uint32_t uslot_fit(uint32_t k)
{
    uint32_t n = k * USLOT_SIZE / (TAPE_BLK / 2u);
    while (n && uslot_span(n) > k)
        n--;
    return n;
}

static uint32_t uslot_seq;                 /* the newest save's seq */

/* the flash under the slots: the target reads through the XIP window and writes with the storage driver; the host
 * test (RF_HOST) brings its own */
#ifndef RF_HOST
#if FELUCCA_FLASH
static const uint8_t rf_blank[sizeof(uslot_hdr_t)] = {0};
static const uint8_t *rf_ptr(uint32_t off) { return flash_ok ? fm1_xip_ptr(off) : rf_blank; }
static int rf_erase(uint32_t off) { return flash_ok ? st_erase(off) : -1; }   /* (not the expected part: hands off) */
static int rf_prog(uint32_t off, const void *src, uint32_t n) { return flash_ok ? st_prog(off, src, n) : -1; }
#else
static const uint8_t rf_blank[sizeof(uslot_hdr_t)] = {0};
static const uint8_t *rf_ptr(uint32_t off) { (void)off; return rf_blank; }
static int rf_erase(uint32_t off) { (void)off; return -1; }
static int rf_prog(uint32_t off, const void *src, uint32_t n) { (void)off; (void)src; (void)n; return -1; }
#endif
#endif

static const uslot_hdr_t *uslot_hdr(uint32_t s) { return (const uslot_hdr_t *)rf_ptr(USLOT_BASE + s * USLOT_SIZE); }

/* zlib CRC-32, 4 bits per step (as storage.c), carried on from c (start: 0xFFFFFFFF; the result: ~c) */
static uint32_t rf_crc_upd(uint32_t c, const void *p, uint32_t n)
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return c;
}
static uint32_t rf_crc32(const void *p, uint32_t n) { return ~rf_crc_upd(0xFFFFFFFFu, p, n); }

/* the CRC a slot's header holds: over the fixed part (magic, seq and crc as 0) and the block arrays, then (apart)
 * over the data; arr: the arrays as stored, data: the blocks end to end (both read through the flash window) */
static uint32_t uslot_crc(const uslot_hdr_t *h, const uint8_t *arr, const uint8_t *data)
{
    uslot_hdr_t c = *h;
    uint32_t k;
    c.magic = c.seq = 0;
    c.crc = 0;
    k = rf_crc_upd(0xFFFFFFFFu, &c, USLOT_FIX);
    k = rf_crc_upd(k, arr, 4u * uslot_n(h->nblk));
    return ~k ^ rf_crc32(data, (uint32_t)h->nblk * (TAPE_BLK / 2u));
}

static uint8_t uslot_ok[USLOT_N];          /* each slot's uslot_check, kept: uslot_names after every change */

/* slot s holds a sound, as last checked (the audio ISR asks every block, through tape_view: a long reel's CRC is
 * too slow to run there, so it runs once, in uslot_names) */
static int uslot_valid(uint32_t s) { return s < USLOT_N && uslot_ok[s]; }

/* slot s starts a sound: its header is whole, it fits the slots after it, and its CRC matches */
static int uslot_check(uint32_t s)
{
    const uslot_hdr_t *h;
    uint32_t off = USLOT_BASE + s * USLOT_SIZE, span;
    if (s >= USLOT_N)
        return 0;
    h = uslot_hdr(s);
    span = h->span ? h->span : 1u;
    return h->magic == USLOT_MAGIC && h->nblk && span <= USLOT_N - s && uslot_span(h->nblk) <= span &&
           h->crc == uslot_crc(h, rf_ptr(off + USLOT_FIX), rf_ptr(off + uslot_doff(h->nblk)));
}

/* slot s is part of a longer reel that starts in a slot before it */
static int uslot_covered(uint32_t s)
{
    uint32_t r;
    for (r = 0; r < s; r++)
        if (uslot_valid(r)) {
            const uslot_hdr_t *h = uslot_hdr(r);
            if (r + (h->span ? h->span : 1u) > s)
                return 1;
        }
    return 0;
}

/* tape.c: what a user slot plays (0: empty) */
static int uslot_view(uint32_t s, tape_view_t *v)
{
    const uslot_hdr_t *h = uslot_hdr(s);
    uint32_t off = USLOT_BASE + s * USLOT_SIZE, n;
    if (!uslot_valid(s))
        return 0;
    n = uslot_n(h->nblk);
    v->data = rf_ptr(off + uslot_doff(h->nblk));
    v->pred = (const int16_t *)rf_ptr(off + USLOT_FIX);
    v->idx = rf_ptr(off + USLOT_FIX + 2u * n);
    v->peak = rf_ptr(off + USLOT_FIX + 3u * n);
    v->len = h->nblk * TAPE_BLK;
    v->ram = 0;
    v->map = 0;
    return 1;
}

/* the slots' names into the REEL list (param.c uslot_name): the stored name, or U1..U6 for an empty slot (and one
 * a longer reel covers) */
static void uslot_names(void)
{
    uint32_t s;
    for (s = 0; s < USLOT_N; s++)
        uslot_ok[s] = (uint8_t)uslot_check(s);
    for (s = 0; s < USLOT_N; s++) {
        const uslot_hdr_t *h = uslot_hdr(s);
        if (uslot_valid(s)) {
            str_cpy(uslot_name[s], h->name, sizeof uslot_name[s]);
            if (h->seq > uslot_seq)
                uslot_seq = h->seq;
        } else {
            uslot_name[s][0] = 'U';
            uslot_name[s][1] = (char)('1' + s);
            uslot_name[s][2] = 0;
        }
    }
}

static void uslot_init(void) { uslot_names(); }

/* the first slot that starts a run of k slots holding nothing (neither a sound nor part of a longer one), or -1 */
static int32_t uslot_free_run(uint32_t k)
{
    uint32_t s, j;
    for (s = 0; s + k <= USLOT_N; s++) {
        for (j = 0; j < k && !uslot_valid(s + j) && !uslot_covered(s + j); j++)
            ;
        if (j == k)
            return (int32_t)s;
    }
    return -1;
}

/* the first free slot, or -1 */
static int32_t uslot_free(void) { return uslot_free_run(1); }

/* the free slots from s on, in a row */
static uint32_t uslot_run_at(uint32_t s)
{
    uint32_t k = 0;
    while (s + k < USLOT_N && !uslot_valid(s + k) && !uslot_covered(s + k))
        k++;
    return k;
}

/* Save nblk blocks of the view v into slot s (main loop), under name (its first four letters and digits, upper
 * case), spanning the slots after s as it needs; a sound longer than the slots from s to the end hold is cut to
 * fit. Returns the blocks saved, or -1 when the flash refused. */
static int32_t uslot_save_view(uint32_t s, const tape_view_t *v, uint32_t nblk, const char *name)
{
    static uslot_hdr_t h;
    static uint8_t arr[256];               /* the block arrays, a piece at a time */
    uint32_t off = USLOT_BASE + s * USLOT_SIZE, i, n = 0, na, span, doff, b, k, crc;
    int rc = 0;
    if (s >= USLOT_N || !nblk)
        return -1;
    if (nblk > uslot_fit(USLOT_N - s))
        nblk = uslot_fit(USLOT_N - s);
    span = uslot_span(nblk);
    na = uslot_n(nblk);
    doff = uslot_doff(nblk);
    memset(&h, 0, sizeof h);
    for (i = 0; name && name[i] && n < 4u; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 32);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
            h.name[n++] = c;
    }
    if (!n) {
        h.name[0] = 'U';
        h.name[1] = (char)('1' + s);
    }
    h.nblk = (uint16_t)nblk;
    h.span = (uint16_t)span;
    for (i = 0; i < span * USLOT_SIZE && !rc; i += 4096u)
        rc = rf_erase(off + i);
    for (b = 0; b < nblk && !rc; b++)                  /* the data first */
        rc = rf_prog(off + doff + b * (TAPE_BLK / 2u), tv_data(v, b), TAPE_BLK / 2u);
    /* the block arrays: pred[na], idx[na], peak[na] (past nblk: zero), into the CRC and the flash a piece at a time */
    crc = rf_crc_upd(0xFFFFFFFFu, &h, USLOT_FIX);      /* (magic, seq, crc are 0 in h now) */
    for (k = 0; k < 4u * na && !rc; k += sizeof arr) {
        uint32_t m = 4u * na - k < sizeof arr ? 4u * na - k : sizeof arr;
        for (i = 0; i < m; i++) {
            uint32_t at = k + i, x = 0;
            if (at < 2u * na) {                        /* pred, little-endian */
                uint32_t bb = at / 2u;
                x = bb < nblk ? (uint32_t)(uint16_t)tv_pred(v, bb) : 0u;
                x = at & 1u ? x >> 8 : x & 0xFFu;
            } else if (at < 3u * na) {
                x = at - 2u * na < nblk ? (uint32_t)tv_idx(v, at - 2u * na) : 0u;
            } else {
                x = at - 3u * na < nblk ? tv_peak(v, at - 3u * na) : 0u;
            }
            arr[i] = (uint8_t)x;
        }
        crc = rf_crc_upd(crc, arr, m);
        rc = rf_prog(off + USLOT_FIX + k, arr, m);
    }
    if (!rc) {
        uint32_t d = 0xFFFFFFFFu;
        for (b = 0; b < nblk; b++)
            d = rf_crc_upd(d, tv_data(v, b), TAPE_BLK / 2u);
        h.crc = ~crc ^ ~d;
        h.seq = ++uslot_seq;
        h.magic = USLOT_MAGIC;
        rc = rf_prog(off, &h, USLOT_FIX);             /* the commit: the fixed part, its magic, last */
    }
    uslot_names();                         /* (a failed save: the slot reads empty, and is named so) */
    return rc ? -1 : (int32_t)nblk;
}

/* the same from arrays in the reel layout (a factory reel's, the tests'); returns 0, or -1 when it didn't all fit
 * or the flash refused */
static int uslot_save(uint32_t s, const uint8_t *data, const int16_t *pred, const uint8_t *idx, const uint8_t *peak,
                      uint32_t nblk, const char *name)
{
    tape_view_t v = {data, pred, idx, peak, nblk * TAPE_BLK, 0, 0};
    return uslot_save_view(s, &v, nblk, name) == (int32_t)nblk ? 0 : -1;
}

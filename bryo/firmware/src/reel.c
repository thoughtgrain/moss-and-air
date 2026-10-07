/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: user reels, your own sounds in flash (docs/bryo-architecture.md, "Files over USB").
 *
 * Six slots in Felucca's user-sample area (0xA0000..0xDBFFF), 40 KiB each, in the tape's own format so a slot
 * plays straight from flash like a factory reel (through the XIP window) and copying it onto a tape is a plain copy:
 *
 *   sector 0     the header: magic, seq, name, length in blocks, each block's decoder state and peak, a CRC over
 *                all of it and the data. It is programmed LAST, so it is the commit record.
 *   sectors 1-9  the ADPCM data, 36,864 bytes: a whole tape
 *
 * A save erases the slot, programs the data, then the header. A save torn by a power cut leaves no valid header:
 * the slot reads empty, never half old and half new. (A/B copies would halve the slots; a slot is a sound you still
 * have on your computer.) Saves happen in the main loop with the audio running: the flash driver turns interrupts
 * off per 256 bytes, so the sound stutters for the second a save takes, and the screen says SAVING.
 *
 * Without the flash driver (a RAM-only build) the slots are always empty. */

#define USLOT_BASE 0xA0000u
#define USLOT_SIZE 0xA000u                 /* 40 KiB: a header sector, 9 data sectors */
#define USLOT_DATA 0x1000u                 /* the data's offset in the slot */
#define USLOT_MAGIC 0x4C454552u            /* "REEL" */

typedef struct {
    uint32_t magic, seq;
    char name[8];                          /* up to 4 letters shown, 0-terminated */
    uint16_t nblk, rsv;
    uint32_t crc;                          /* over the header from name on (crc 0) and the data */
    int16_t pred[TAPE_NBLK];
    uint8_t idx[TAPE_NBLK];
    uint8_t peak[TAPE_NBLK];
} uslot_hdr_t;

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

static uint32_t rf_crc32(const void *p, uint32_t n)       /* zlib CRC-32, 4 bits per step (as storage.c) */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}

static uint32_t uslot_crc(const uslot_hdr_t *h, const uint8_t *data)
{
    static uslot_hdr_t c;
    c = *h;
    c.magic = c.seq = 0;
    c.crc = 0;
    return rf_crc32(&c, sizeof c) ^ rf_crc32(data, (uint32_t)h->nblk * (TAPE_BLK / 2u));
}

/* slot s holds a sound: its header is whole and its CRC matches */
static int uslot_valid(uint32_t s)
{
    const uslot_hdr_t *h = uslot_hdr(s);
    return s < USLOT_N && h->magic == USLOT_MAGIC && h->nblk && h->nblk <= TAPE_NBLK &&
           h->crc == uslot_crc(h, rf_ptr(USLOT_BASE + s * USLOT_SIZE + USLOT_DATA));
}

/* tape.c: what a user slot plays (0: empty) */
static int uslot_view(uint32_t s, tape_view_t *v)
{
    const uslot_hdr_t *h = uslot_hdr(s);
    if (!uslot_valid(s))
        return 0;
    v->data = rf_ptr(USLOT_BASE + s * USLOT_SIZE + USLOT_DATA);
    v->pred = h->pred;
    v->idx = h->idx;
    v->peak = h->peak;
    v->len = h->nblk * TAPE_BLK;
    v->ram = 0;
    return 1;
}

/* the slots' names into the REEL list (param.c uslot_name): the stored name, or U1..U6 for an empty slot */
static void uslot_names(void)
{
    uint32_t s;
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

/* the first slot without a sound, or -1 */
static int32_t uslot_free(void)
{
    uint32_t s;
    for (s = 0; s < USLOT_N; s++)
        if (!uslot_valid(s))
            return (int32_t)s;
    return -1;
}

/* save a sound into slot s (main loop): nblk blocks of data with their states and peaks, under name (its first four
 * letters and digits, upper case). Returns 0, or -1 when the flash refused. */
static int uslot_save(uint32_t s, const uint8_t *data, const int16_t *pred, const uint8_t *idx, const uint8_t *peak,
                      uint32_t nblk, const char *name)
{
    static uslot_hdr_t h;                  /* (1.2 KB: not on the stack) */
    uint32_t off = USLOT_BASE + s * USLOT_SIZE, i, n = 0;
    int rc = 0;
    if (s >= USLOT_N || !nblk || nblk > TAPE_NBLK)
        return -1;
    for (i = 0; i < sizeof h; i++)
        ((uint8_t *)&h)[i] = 0;
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
    for (i = 0; i < nblk; i++) {
        h.pred[i] = pred[i];
        h.idx[i] = idx[i];
        h.peak[i] = peak[i];
    }
    h.crc = uslot_crc(&h, data);
    h.seq = ++uslot_seq;
    h.magic = USLOT_MAGIC;
    for (i = 0; i < USLOT_SIZE && !rc; i += 4096u)
        rc = rf_erase(off + i);
    if (!rc)
        rc = rf_prog(off + USLOT_DATA, data, nblk * (TAPE_BLK / 2u));
    if (!rc)
        rc = rf_prog(off, &h, sizeof h);
    uslot_names();                         /* (a failed save: the slot reads empty, and is named so) */
    return rc ? -1 : 0;
}

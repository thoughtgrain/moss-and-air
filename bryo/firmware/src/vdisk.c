/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: the drive. Plugged in, the FM-1 shows up as a small USB drive named BRYO (usb.c, Mass Storage), and this
 * is the disk behind it: a 64 MiB FAT12 volume made up on the fly from what Bryo holds (docs/bryo-architecture.md,
 * "Files over USB").
 *
 *   TAPE1.WAV .. TAPE4.WAV   what each track plays now (its tape, or the reel it has chosen)
 *   REEL1.WAV .. REEL4.WAV   the factory reels
 *   USER1.WAV .. USER6.WAV   your reels, the ones that hold a sound
 *   README.TXT               what this drive is and how to use it
 *
 * Reading: a WAV is made as the computer reads it (a 44-byte header, then the sound decoded from ADPCM to 16-bit
 * PCM at 22,050 Hz, mono), so copying a file off the drive needs no conversion step and no RAM copy.
 *
 * Writing is best effort, because a drive doesn't get told "here is a file called X": the computer writes clusters
 * of data, FAT sectors and directory entries in its own order. So:
 *   - a data sector that starts "RIFF....WAVE" starts a capture; the sectors after it are taken as the rest of the
 *     file as they arrive in order (a fresh volume gives a new file contiguous clusters), and are converted on the
 *     way in: any rate, 8/16/24/32-bit PCM or 32-bit float, any number of channels, to 22,050 Hz mono ADPCM, into
 *     chunks of the shared memory (mem.c) as it arrives: the free ones, then cleared tapes' and parked tracks',
 *     never a tape in use. What doesn't fit is read and dropped, and the message says the sound was cut;
 *   - a directory sector that points an entry at the capture's first cluster names it: TAPEn.WAV replaces track
 *     n's tape, USERn.WAV replaces your reel n, any other name goes to the first free user reel, named after the
 *     file (its first four letters and digits). A file that never gets a name in the root (dropped into a folder)
 *     goes to a free user reel after 3 s;
 *   - the FAT and the root directory the computer writes are kept in RAM and read back as written, so its view
 *     stays consistent until you eject; other small writes (a folder, macOS's ._ files) are kept in a 16-sector
 *     cache. What's new shows up as files after you eject and plug the FM-1 back in.
 * What doesn't work, by design for now: deleting a file in the computer's file manager doesn't delete the sound
 * (clear it on the FM-1), and a WAV split into scattered pieces (an old, full volume) can arrive garbled.
 *
 * Main loop only: usb.c calls vdisk_read / vdisk_write from usb_poll, and vdisk_poll commits a finished capture.
 * Integer only. */

#define VD_SEC 512u
#define VD_TOTAL 130048u                    /* sectors: 63.5 MiB (FAT12 tops out at 4,084 clusters) */
#define VD_SPC 32u                          /* sectors per cluster: 16 KiB */
#define VD_FATS 2u
#define VD_FATSEC 12u                       /* sectors per FAT: 4096 12-bit entries */
#define VD_REGION 128u                      /* clusters each file is given (2 MiB): a bigger file written over it,
                                             * or into the hole it leaves, still lands in one run */
#define VD_ROOTN 64u                        /* root directory entries */
#define VD_FAT0 1u                          /* the first FAT's first sector */
#define VD_ROOT (VD_FAT0 + VD_FATS * VD_FATSEC)            /* 13 */
#define VD_DATA (VD_ROOT + VD_ROOTN * 32u / VD_SEC)         /* 29: cluster 2 */
#define VD_NCLUS ((VD_TOTAL - VD_DATA) / VD_SPC)            /* 4063 */
#define VD_CLUS (VD_SPC * VD_SEC)
#define VD_CACHE 16u                        /* other written sectors kept (folders, metadata) */
#define VD_NAME_MS 3000u                    /* a capture with no name in the root after this: a free user reel */

enum { VF_README, VF_TAPE, VF_REEL, VF_USER };
typedef struct {
    char name[11];                          /* 8.3, space-padded */
    uint8_t kind, idx;
    uint32_t size, clus;                    /* bytes; first cluster (file i starts at cluster 2 + i x VD_REGION) */
} vfile_t;

static struct {
    uint8_t fat[VD_FATSEC * VD_SEC];        /* one FAT, served as both copies */
    uint8_t root[VD_ROOTN * 32u];
    uint8_t cache[VD_CACHE][VD_SEC];
    uint32_t cache_lba[VD_CACHE];
    uint32_t cache_next;
    vfile_t f[1u + NTRK + NREEL + USLOT_N];
    uint32_t nf;
    tape_rd_t rd;                           /* the block being read out as a WAV */
    volatile uint8_t ready;                 /* the volume is made (msc.c answers "becoming ready" until then) */
    volatile uint8_t remount;               /* usb.c: a bus reset; the main loop makes the volume afresh */
    volatile uint8_t busy;                  /* the main loop is committing a capture: no new commands */
    volatile uint8_t msg;                   /* a capture started (the main loop says so on screen) */
} vd __attribute__((section(".pool")));

static struct {                             /* the capture of a dropped WAV */
    uint8_t on, done, named, failed;
    int8_t dest;                            /* 0..3 track n's tape; 4..9 user reel n-4; -1 the first free user reel */
    uint32_t next;                          /* the sector expected next */
    uint32_t clus0;                         /* the cluster it starts in */
    uint32_t t_done;                        /* when it finished (ms) */
    char name[12];
    /* the RIFF reader */
    uint8_t st;                             /* 0 RIFF header, 1 a chunk's header, 2 fmt, 3 data, 4 skip, 5 done */
    uint8_t hb[12];
    uint32_t hn, need;
    uint8_t fmt[40];
    uint32_t fn;
    uint16_t tag, ch, bits, bpf;            /* format, channels, bits, bytes per frame */
    uint32_t rate;
    uint8_t fb[32];                         /* a frame being assembled */
    uint32_t fbn;
    /* the rate converter and the encoder */
    uint32_t t;
    int32_t acc, accn, last;
    int16_t blk[TAPE_BLK];
    uint32_t bn, nblk, pk;
    int32_t pred, idx;
    uint8_t cut;                            /* memory ran out: the rest was dropped */
    uint8_t nch;                            /* the chunks it fills (mem.c, owner MEM_IMPORT) */
    uint8_t map[MEM_NC];
} cap;

/* the capture's chunks back to the pool */
static void cap_free(void)
{
    while (cap.nch)
        mem_free(cap.map[--cap.nch]);
}

/* the capture as a view (vdisk_commit copies or saves it from here) */
static void cap_view(tape_view_t *v)
{
    memset(v, 0, sizeof *v);
    v->map = cap.map;
    v->len = cap.nblk * TAPE_BLK;
    v->ram = 1;
}

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }
static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, v); wr16(p + 2, v >> 16); }

static uint32_t fat_get(uint32_t c)
{
    uint32_t o = c + c / 2u, v = rd16(vd.fat + o);
    return c & 1u ? v >> 4 : v & 0xFFFu;
}

static void fat_set(uint32_t c, uint32_t v)
{
    uint32_t o = c + c / 2u, w = rd16(vd.fat + o);
    w = c & 1u ? (w & 0x000Fu) | (v << 4) : (w & 0xF000u) | (v & 0xFFFu);
    wr16(vd.fat + o, w);
}

static const char VD_README[] =
    "BRYO\r\n\r\n"
    "This drive is your FM-1 running Bryo.\r\n\r\n"
    "TAPE1-4.WAV  what each track plays now. Copy them off to keep them.\r\n"
    "REEL1-4.WAV  the factory reels.\r\n"
    "USER1-6.WAV  your reels.\r\n\r\n"
    "To put a sound on the FM-1, copy a WAV onto this drive:\r\n"
    "  named TAPE1.WAV .. TAPE4.WAV it replaces that track's tape;\r\n"
    "  named USER1.WAV .. USER6.WAV it replaces that reel;\r\n"
    "  any other name goes to the first free user reel, named after the file.\r\n"
    "Any WAV works, mono at 22,050 Hz on the FM-1. A tape keeps as much as its\r\n"
    "free memory holds; a user reel up to 21 seconds, taking the reels after it.\r\n"
    "Copy one file at a time, onto an empty part of the drive, and eject before\r\n"
    "unplugging. New sounds show up as files after you plug the FM-1 back in.\r\n"
    "Deleting a file here doesn't delete the sound: clear it on the FM-1.\r\n";

/* the view a file shows (0: README) */
static int vd_view(const vfile_t *f, tape_view_t *v)
{
    memset(v, 0, sizeof *v);
    if (f->kind == VF_TAPE) {
        tape_view(f->idx, v);
        return 1;
    }
    if (f->kind == VF_REEL) {
        const reel_t *r = &REELS[f->idx];
        v->data = r->data;
        v->pred = r->pred;
        v->idx = r->idx;
        v->peak = r->peak;
        v->len = r->nblk * TAPE_BLK;
        v->ram = 0;
        return 1;
    }
    if (f->kind == VF_USER)
        return uslot_view(f->idx, v);
    return 0;
}

static void vd_add(const char *base, uint32_t n, uint32_t kind, uint32_t idx, uint32_t size)
{
    vfile_t *f = &vd.f[vd.nf];
    uint32_t i;
    for (i = 0; i < 11u; i++)
        f->name[i] = ' ';
    for (i = 0; base[i] && i < 8u; i++)
        f->name[i] = base[i];
    if (n)
        f->name[i] = (char)('0' + n);
    f->name[8] = kind == VF_README ? 'T' : 'W';
    f->name[9] = kind == VF_README ? 'X' : 'A';
    f->name[10] = kind == VF_README ? 'T' : 'V';
    f->kind = (uint8_t)kind;
    f->idx = (uint8_t)idx;
    f->size = size;
    vd.nf++;
}

/* plugged in (or the medium reloaded): the volume as Bryo holds it now */
static void vdisk_mount(void)
{
    uint32_t i, c, k;
    tape_view_t v;
    for (i = 0; i < sizeof vd.fat; i++)
        vd.fat[i] = 0;
    for (i = 0; i < sizeof vd.root; i++)
        vd.root[i] = 0;
    for (i = 0; i < VD_CACHE; i++)
        vd.cache_lba[i] = 0xFFFFFFFFu;
    vd.nf = 0;
    vd.rd.ok = 0;
    vd.ready = 1;
    vd_add("README", 0, VF_README, 0, sizeof VD_README - 1u);
    for (i = 0; i < NTRK; i++) {
        tape_view(i, &v);
        vd_add("TAPE", i + 1u, VF_TAPE, i, 44u + v.len * 2u);
    }
    for (i = 0; i < NREEL; i++)
        vd_add("REEL", i + 1u, VF_REEL, i, 44u + REELS[i].nblk * TAPE_BLK * 2u);
    for (i = 0; i < USLOT_N; i++)
        if (uslot_view(i, &v))
            vd_add("USER", i + 1u, VF_USER, i, 44u + v.len * 2u);
    fat_set(0, 0xFF8u);                      /* media descriptor, end of chain */
    fat_set(1, 0xFFFu);
    memcpy(vd.root, "BRYO       ", 11);      /* the volume label */
    vd.root[11] = 0x08;
    for (i = 0; i < vd.nf; i++) {           /* the files end to end, each a chain */
        vfile_t *f = &vd.f[i];
        uint8_t *e = vd.root + 32u * (i + 1u);
        uint32_t n = (f->size + VD_CLUS - 1u) / VD_CLUS;
        c = 2u + i * VD_REGION;
        f->clus = n ? c : 0;
        for (k = 0; k < n; k++, c++)
            fat_set(c, k + 1u < n ? c + 1u : 0xFFFu);
        memcpy(e, f->name, 11);
        e[11] = f->kind == VF_REEL ? 0x01 : 0x00;   /* the factory reels read-only */
        wr16(e + 26, f->clus);
        wr32(e + 28, f->size);
        wr16(e + 24, (46u << 9) | (10u << 5) | 7u);  /* 2026-10-07 */
        wr16(e + 16, (46u << 9) | (10u << 5) | 7u);
    }
}

/* the boot sector */
static void vd_boot(uint8_t *b)
{
    static const uint8_t JMP[3] = {0xEB, 0x3C, 0x90};
    uint32_t i;
    for (i = 0; i < VD_SEC; i++)
        b[i] = 0;
    memcpy(b, JMP, 3);
    memcpy(b + 3, "BRYO    ", 8);
    wr16(b + 11, VD_SEC);
    b[13] = VD_SPC;
    wr16(b + 14, VD_FAT0);
    b[16] = VD_FATS;
    wr16(b + 17, VD_ROOTN);
    wr16(b + 19, 0);                         /* (over 65,535 sectors: the 32-bit count at 32) */
    wr32(b + 32, VD_TOTAL);
    b[21] = 0xF8;
    wr16(b + 22, VD_FATSEC);
    wr16(b + 24, 32);                        /* sectors per track, heads: nominal */
    wr16(b + 26, 64);
    b[36] = 0x80;
    b[38] = 0x29;                            /* the extended boot signature */
    wr32(b + 39, 0x42525930u);               /* volume serial */
    memcpy(b + 43, "BRYO       ", 11);
    memcpy(b + 54, "FAT12   ", 8);
    b[510] = 0x55;
    b[511] = 0xAA;
}

/* byte range [o, o + n) of file f into b */
static void vd_file_bytes(const vfile_t *f, uint32_t o, uint8_t *b, uint32_t n)
{
    tape_view_t v;
    uint8_t h[44];
    uint32_t i;
    if (f->kind == VF_README) {
        for (i = 0; i < n; i++)
            b[i] = o + i < sizeof VD_README - 1u ? (uint8_t)VD_README[o + i] : 0;
        return;
    }
    if (!vd_view(f, &v))
        v.len = 0;
    memcpy(h, "RIFF", 4);
    wr32(h + 4, f->size - 8u);
    memcpy(h + 8, "WAVEfmt ", 8);
    wr32(h + 16, 16);
    wr16(h + 20, 1);                         /* PCM */
    wr16(h + 22, 1);                         /* mono */
    wr32(h + 24, TAPE_SR);
    wr32(h + 28, TAPE_SR * 2u);
    wr16(h + 32, 2);
    wr16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    wr32(h + 40, f->size - 44u);
    for (i = 0; i < n; i++) {
        uint32_t p = o + i;
        if (p < 44u) {
            b[i] = h[p];
        } else if (p < f->size) {
            uint32_t s = (p - 44u) / 2u;
            int32_t x = s < v.len ? tape_at(&v, &vd.rd, (int32_t)s) : 0;
            b[i] = (uint8_t)((p - 44u) & 1u ? (uint32_t)x >> 8 : (uint32_t)x);
        } else {
            b[i] = 0;
        }
    }
}

/* the computer reads sector lba */
static void vdisk_read(uint32_t lba, uint8_t *b)
{
    uint32_t i;
    if (lba == 0) {
        vd_boot(b);
        return;
    }
    if (lba < VD_ROOT) {                     /* either FAT copy */
        memcpy(b, vd.fat + ((lba - VD_FAT0) % VD_FATSEC) * VD_SEC, VD_SEC);
        return;
    }
    if (lba < VD_DATA) {
        memcpy(b, vd.root + (lba - VD_ROOT) * VD_SEC, VD_SEC);
        return;
    }
    for (i = 0; i < VD_CACHE; i++)
        if (vd.cache_lba[i] == lba) {
            memcpy(b, vd.cache[i], VD_SEC);
            return;
        }
    {
        uint32_t c = 2u + (lba - VD_DATA) / VD_SPC, so = ((lba - VD_DATA) % VD_SPC) * VD_SEC;
        for (i = 0; i < vd.nf; i++) {
            const vfile_t *f = &vd.f[i];
            uint32_t n = (f->size + VD_CLUS - 1u) / VD_CLUS;
            if (f->clus && c >= f->clus && c < f->clus + n) {
                vd_file_bytes(f, (c - f->clus) * VD_CLUS + so, b, VD_SEC);
                return;
            }
        }
    }
    for (i = 0; i < VD_SEC; i++)
        b[i] = 0;
}

/* ------------------------------------------------------------ capture --- */
static void cap_emit(int32_t x)              /* one sample at 22,050 Hz into the capture's chunks */
{
    if (cap.cut)
        return;
    x = clamp(x, -32767, 32767);
    cap.blk[cap.bn++] = (int16_t)x;
    if ((uint32_t)(x < 0 ? -x : x) > cap.pk)
        cap.pk = (uint32_t)(x < 0 ? -x : x);
    if (cap.bn == TAPE_BLK) {
        mem_chunk_t *m;
        uint32_t o = cap.nblk % MEM_CB;
        if (cap.nblk / MEM_CB >= cap.nch) {            /* a new chunk: a free one, else a cleared or parked tape's */
            int32_t k = cap.nch < TAPE_MAXCH ? tape_alloc(MEM_IMPORT, NTRK, 0) : -1;   /* (a tape's most) */
            if (k < 0) {
                cap.cut = 1;
                cap.bn = 0;
                return;
            }
            cap.map[cap.nch++] = (uint8_t)k;
        }
        m = mem_at(cap.map[cap.nblk / MEM_CB]);
        m->pred[o] = (int16_t)cap.pred;
        m->idx[o] = (uint8_t)cap.idx;
        ima_enc_st(cap.blk, &cap.pred, &cap.idx, m->data[o], TAPE_BLK);
        m->peak[o] = (uint8_t)(cap.pk >> 7 > 255u ? 255u : cap.pk >> 7);
        cap.nblk++;
        cap.bn = 0;
        cap.pk = 0;
    }
}

static void cap_frame(void)                  /* a whole input frame: mixed to mono, converted to 22,050 Hz */
{
    uint32_t c, bps = cap.bits / 8u;
    int32_t sum = 0, x;
    for (c = 0; c < cap.ch; c++) {
        const uint8_t *p = cap.fb + c * bps;
        if (cap.tag == 3)                    /* float32: the exponent and mantissa, by hand (no float) */
        {
            uint32_t u = rd32(p), e = (u >> 23) & 0xFFu, m = (u & 0x7FFFFFu) | 0x800000u;
            int32_t v = e < 103u ? 0 : e >= 127u ? 32767 : (int32_t)(m >> (127u - e + 8u));
            x = (u >> 31) ? -v : v;
        } else if (bps == 1u) {
            x = ((int32_t)p[0] - 128) << 8;
        } else if (bps == 2u) {
            x = (int16_t)rd16(p);
        } else {                             /* 24 and 32 bit: the top 16 */
            x = (int16_t)rd16(p + bps - 2u);
        }
        sum += x;
    }
    x = sum / (int32_t)cap.ch;
    if (cap.rate >= TAPE_SR) {               /* down: the average of the frames under each output sample */
        cap.acc += x;
        cap.accn++;
        cap.t += TAPE_SR;
        while (cap.t >= cap.rate) {
            cap.t -= cap.rate;
            cap_emit(cap.acc / cap.accn);
            cap.acc = cap.accn = 0;
        }
    } else {                                 /* up: each frame held as long as it lasts */
        cap.t += TAPE_SR;
        while (cap.t >= cap.rate) {
            cap.t -= cap.rate;
            cap_emit(x);
        }
    }
}

/* n bytes of the file, in order */
static void cap_bytes(const uint8_t *b, uint32_t n)
{
    uint32_t i = 0;
    if (cap.failed)
        cap.done = 1;
    while (i < n && !cap.failed) {
        switch (cap.st) {
        case 0:                              /* "RIFF" size "WAVE" */
            cap.hb[cap.hn++] = b[i++];
            if (cap.hn == 12u) {
                if (memcmp(cap.hb, "RIFF", 4) || memcmp(cap.hb + 8, "WAVE", 4))
                    cap.failed = 1;
                cap.hn = 0;
                cap.st = 1;
            }
            break;
        case 1:                              /* a chunk's id and size */
            cap.hb[cap.hn++] = b[i++];
            if (cap.hn == 8u) {
                cap.need = rd32(cap.hb + 4);
                cap.hn = 0;
                cap.fn = 0;
                if (!memcmp(cap.hb, "fmt ", 4)) {
                    cap.st = 2;
                } else if (!memcmp(cap.hb, "data", 4)) {
                    if (!cap.ch || !cap.rate || !cap.bpf || cap.bpf > sizeof cap.fb ||
                        !((cap.tag == 1 && cap.bits >= 8 && cap.bits <= 32 && !(cap.bits & 7u)) ||
                          (cap.tag == 3 && cap.bits == 32)))
                        cap.failed = 1;      /* (no fmt before the data, or a format Bryo can't read) */
                    cap.st = 3;
                } else {
                    cap.st = 4;
                }
                if (cap.st != 3 && (cap.need & 1u))
                    cap.need++;              /* (chunks are padded to even lengths) */
            }
            break;
        case 2:                              /* fmt: the format */
            if (!cap.need) {
                cap.st = 1;
                break;
            }
            if (cap.fn < sizeof cap.fmt)
                cap.fmt[cap.fn] = b[i];
            cap.fn++;
            i++;
            if (!--cap.need) {
                cap.tag = (uint16_t)rd16(cap.fmt);
                cap.ch = (uint16_t)rd16(cap.fmt + 2);
                cap.rate = rd32(cap.fmt + 4);
                cap.bpf = (uint16_t)rd16(cap.fmt + 12);
                cap.bits = (uint16_t)rd16(cap.fmt + 14);
                if (cap.tag == 0xFFFEu && cap.fn >= 26u)
                    cap.tag = (uint16_t)rd16(cap.fmt + 24);   /* WAVE_FORMAT_EXTENSIBLE: the subformat */
                cap.st = 1;
            }
            break;
        case 3:                              /* data: frames */
            while (i < n && cap.need) {
                cap.fb[cap.fbn++] = b[i++];
                cap.need--;
                if (cap.fbn == cap.bpf) {
                    cap_frame();
                    cap.fbn = 0;
                }
            }
            if (!cap.need) {
                while (cap.bn)               /* the last block, filled with silence */
                    cap_emit(0);
                cap.st = 5;
                cap.done = 1;
                cap.t_done = fm1_ms;
            }
            break;
        case 4:                              /* a chunk Bryo doesn't need */
            if (cap.need) {
                i++;
                cap.need--;
            }
            if (!cap.need)
                cap.st = 1;
            break;
        default:
            return;
        }
    }
    if (cap.failed) {                        /* (a format Bryo can't read: over now, vdisk_poll says so) */
        cap.done = 1;
        cap.t_done = fm1_ms;
    }
}

static void cap_start(uint32_t lba)
{
    uint32_t i;
    cap_free();                              /* (a capture still arriving is dropped) */
    for (i = 0; i < sizeof cap; i++)
        ((uint8_t *)&cap)[i] = 0;
    cap.on = 1;
    cap.dest = -1;
    cap.next = lba;
    cap.clus0 = 2u + (lba - VD_DATA) / VD_SPC;
    vd.msg = 1;                              /* (usb_poll's context: the main loop draws the message) */
}

/* the root directory as written: is the capture named? (LFN entries before an 8.3 entry give its long name) */
static void cap_name(void)
{
    char lfn[16];
    uint32_t i, ln = 0;
    lfn[0] = 0;
    for (i = 0; i < VD_ROOTN && cap.on && !cap.named; i++) {
        const uint8_t *e = vd.root + 32u * i;
        if (e[0] == 0)
            break;
        if (e[0] == 0xE5u)
            continue;
        if (e[11] == 0x0Fu) {                /* a long name part: the first part (ordinal 1) holds its start */
            if ((e[0] & 0x3Fu) == 1u) {
                static const uint8_t AT[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
                uint32_t k;
                for (k = 0, ln = 0; k < 13u && ln < sizeof lfn - 1u; k++) {
                    uint32_t ch = rd16(e + AT[k]);
                    if (!ch || ch == 0xFFFFu)
                        break;
                    lfn[ln++] = ch < 128u ? (char)ch : '_';
                }
                lfn[ln] = 0;
            }
            continue;
        }
        if (!(e[11] & 0x18u) && rd16(e + 26) == cap.clus0 && rd32(e + 28)) {
            uint32_t k;
            cap.named = 1;
            for (k = 0; k < 8u && e[k] != ' '; k++)
                cap.name[k] = (char)e[k];
            cap.name[k] = 0;
            if (!memcmp(e, "TAPE", 4) && e[4] >= '1' && e[4] < '1' + NTRK && e[5] == ' ')
                cap.dest = (int8_t)(e[4] - '1');
            else if (!memcmp(e, "USER", 4) && e[4] >= '1' && e[4] < (uint8_t)('1' + USLOT_N) && e[5] == ' ')
                cap.dest = (int8_t)(4 + e[4] - '1');
            else if (ln)
                str_cpy(cap.name, lfn, sizeof cap.name);
        }
        ln = 0;
        lfn[0] = 0;
    }
}

/* the computer writes sector lba */
static void vdisk_write(uint32_t lba, const uint8_t *b)
{
    uint32_t i;
    if (lba == 0 || lba >= VD_TOTAL)
        return;
    if (lba < VD_ROOT) {
        memcpy(vd.fat + ((lba - VD_FAT0) % VD_FATSEC) * VD_SEC, b, VD_SEC);
        return;
    }
    if (lba < VD_DATA) {
        memcpy(vd.root + (lba - VD_ROOT) * VD_SEC, b, VD_SEC);
        if (cap.on && !cap.named)
            cap_name();
        return;
    }
    if (!memcmp(b, "RIFF", 4) && !memcmp(b + 8, "WAVE", 4) && !(cap.on && !cap.done && lba == cap.next)) {
        cap_start(lba);                      /* a WAV begins here (one still arriving is dropped) */
    }
    if (cap.on && !cap.done && lba != cap.next && cap.next > VD_DATA && !((cap.next - VD_DATA) % VD_SPC) &&
        !((lba - VD_DATA) % VD_SPC)) {       /* a jump at a cluster's end: taken when the FAT chains it there */
        uint32_t cur = 2u + (cap.next - 1u - VD_DATA) / VD_SPC;
        if (fat_get(cur) == 2u + (lba - VD_DATA) / VD_SPC)
            cap.next = lba;
    }
    if (cap.on && !cap.done && lba == cap.next) {
        cap.next++;
        cap_bytes(b, VD_SEC);
        if (!cap.named)
            cap_name();                      /* (the name may have been written before the data) */
        return;
    }
    for (i = 0; i < VD_CACHE && vd.cache_lba[i] != lba; i++)
        ;
    if (i == VD_CACHE) {
        i = vd.cache_next;
        vd.cache_next = (vd.cache_next + 1u) % VD_CACHE;
    }
    vd.cache_lba[i] = lba;
    memcpy(vd.cache[i], b, VD_SEC);
}

static void vdisk_commit(void);

/* main loop: a fresh volume after a bus reset; a finished capture, once named (or after VD_NAME_MS without a name),
 * to where it goes */
static void vdisk_poll(void)
{
    if (vd.remount) {                        /* plugged in: the volume as Bryo holds it now */
        vd.remount = 0;
        vdisk_mount();
    }
    if (vd.msg) {
        vd.msg = 0;
        ui_message("RECEIVING A WAV OVER USB");
    }
    if (!cap.on || !cap.done)
        return;
    if (cap.failed || !cap.nblk) {
        cap.on = 0;
        cap_free();
        ui_message(cap.cut ? "NO MEMORY FREE FOR THAT WAV" : "THAT WAV COULDN'T BE READ");
        return;
    }
    if (!cap.named && (uint32_t)(fm1_ms - cap.t_done) < VD_NAME_MS)
        return;
    vd.busy = 1;                             /* (the inbox is read below: no new command until it's done) */
    cap.on = 0;
    vdisk_commit();
    vd.busy = 0;
}

/* the finished capture, to its place (main loop, vd.busy) */
static void vdisk_commit(void)
{
    char m[40];
    tape_view_t v;
    int32_t s, saved;
    uint32_t k, cut = cap.cut;
    if (cap.dest >= 0 && cap.dest < (int8_t)NTRK) {             /* a track's tape: the chunks change hands */
        uint32_t t = (uint32_t)cap.dest;
        tape_ctl_t *c = &tape_ctl[t];
        c->rec_ok = 0;
        c->grow = 0;
        c->empty = 1;                                            /* (silent while it's swapped) */
        tape_free(t);
        for (k = 0; k < cap.nch; k++) {
            c->map[k] = cap.map[k];
            mem_give(cap.map[k], MEM_TAPE + t);
        }
        RING_PUBLISH();
        c->nch = cap.nch;
        c->nblk = (uint16_t)cap.nblk;
        cap.nch = 0;
        tp[t].dev[DEV_SRC][TK_REEL] = 0;
        sys.rec &= (uint8_t)~(1u << t);
        if (tape_undo.valid && tape_undo.trk == t)
            tape_undo.valid = 0;
        RING_PUBLISH();
        c->empty = 0;
        tape_ver[t]++;
        str_cpy(m, "TRACK ", sizeof m);
        fmt_int(m + 6, (int32_t)t + 1);
        str_cpy(m + str_len(m), cut ? "'S TAPE: CUT, MEMORY FULL" : "'S TAPE REPLACED", 26);
        ui_message(m);
        return;
    }
    cap_view(&v);
    s = cap.dest >= (int8_t)NTRK ? cap.dest - (int32_t)NTRK : uslot_free_run(uslot_span(cap.nblk));
    if (s < 0)
        s = uslot_free();                                        /* (not enough slots in a row: as much as fits) */
    if (s < 0) {
        cap_free();
        ui_message("NO FREE REEL: NAME IT USER1-6.WAV");
        return;
    }
    ui_message("SAVING THE REEL");
    k = cap.nblk;
    if (cap.dest < (int8_t)NTRK && k > uslot_fit(uslot_run_at((uint32_t)s)))
        k = uslot_fit(uslot_run_at((uint32_t)s));               /* (a free reel: never over the next one) */
    saved = uslot_save_view((uint32_t)s, &v, k, cap.dest >= (int8_t)NTRK ? 0 : cap.name);
    cap_free();
    if (saved < 0) {
        ui_message("THE FLASH REFUSED THE SAVE");
        return;
    }
    str_cpy(m, "SAVED AS REEL ", sizeof m);
    str_cpy(m + str_len(m), uslot_name[s], 6);
    if (cut || (uint32_t)saved < cap.nblk)
        str_cpy(m + str_len(m), " (CUT)", 7);
    ui_message(m);
}

#if BRYO_MSC
/* the drive's disk, for msc.c (usb_poll's context) */
static uint32_t msc_blocks(void) { return VD_TOTAL; }
static int msc_ready(void) { return vd.ready && !vd.busy; }
static void msc_read(uint32_t lba, uint8_t *b) { vdisk_read(lba, b); }
static void msc_write(uint32_t lba, const uint8_t *b) { vdisk_write(lba, b); }
static void msc_eject(void) {}
static void msc_attached(void)
{
    vd.ready = 0;
    vd.remount = 1;
}
#endif

/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: the memory every track's sound shares (docs/bryo-architecture.md, "Memory by usage").
 *
 * Instead of a fixed tape per track, there's one pool of chunks, and each track takes what it uses: its tape grows
 * as it records, GRAIN's live buffer takes a few bars while GRAIN is on, a dry SYNTH track takes nothing. A chunk
 * is 16 of the tape's blocks (4,096 samples, 186 ms at 22.05 kHz) with each block's decoder state and peak, so any
 * chunk holds tape-format sound and a list of chunks reads like one tape (tape.c's views carry the list).
 *
 * Chunks live in two places: most in the pool, the rest in main RAM's spare room (the pool alone can't fit them).
 * mem_at() hides that.
 *
 * Who decides: the main loop allocates and frees, and nothing else may: the audio ISR only reads and writes the
 * chunks an owner's list hands it, below the list's published length, and a WAV arriving over USB (TIMER5's
 * usb_poll, which can cut into the main loop) only takes chunks the main loop set aside for it (vdisk.c). Taking a
 * chunk away is: shorten the list (the length first), then free it. That's safe to reuse at once because the main loop never runs inside the audio ISR: a block the ISR is
 * rendering finishes before the main loop's next step, and every later block sees the shorter list. The ISR keeps
 * no pointer into a chunk from one block to the next (readers copy what they decode; the tape's writer re-checks
 * the list before it commits). Who to take from when nothing is free is tape.c's call (tape_steal): this file keeps
 * the books. Integer only. */

#define MEM_CB 16u                   /* tape blocks per chunk */
#define MEM_NC_POOL 128u             /* chunks in the pool (270 KB) */
#define MEM_NC_RAM 24u               /* chunks in main RAM (50 KB) */
#define MEM_NC (MEM_NC_POOL + MEM_NC_RAM)   /* 152: 28.2 s of tape-format sound in all */
#define MEM_FREE 0xFFu               /* owner: nobody */
/* owners: a track's tape (MEM_TAPE + t), its GRAIN buffer (MEM_GRAIN + t), a WAV arriving over USB (MEM_IMPORT),
 * its RESONATOR's strings (MEM_RESO + t) */
enum { MEM_TAPE = 0, MEM_GRAIN = 4, MEM_IMPORT = 8, MEM_SPARE = 9, MEM_RESO = 10 };   /* (SPARE: kept ready for a
                                                                                       * WAV, vdisk.c; RESO + t: a
                                                                                       * track's strings) */

typedef struct {
    uint8_t data[MEM_CB][128];       /* 16 blocks of 256 samples, 4 bits each */
    int16_t pred[MEM_CB];            /* each block's decoder state at its start */
    uint8_t idx[MEM_CB];
    uint8_t peak[MEM_CB];            /* each block's peak, for the screen */
} mem_chunk_t;

static mem_chunk_t mem_pool[MEM_NC_POOL] __attribute__((section(".pool")));
static mem_chunk_t mem_ram[MEM_NC_RAM];
static uint8_t mem_owner[MEM_NC];

static inline mem_chunk_t *mem_at(uint32_t c) { return c < MEM_NC_POOL ? &mem_pool[c] : &mem_ram[c - MEM_NC_POOL]; }

static void mem_init(void)
{
    uint32_t c;
    for (c = 0; c < MEM_NC; c++)
        mem_owner[c] = MEM_FREE;
}

/* main loop: a free chunk for owner o, silent (zero nibbles from a zero state), or -1 when none is free */
static int32_t mem_alloc(uint32_t o)
{
    uint32_t c;
    for (c = 0; c < MEM_NC; c++)
        if (mem_owner[c] == MEM_FREE) {
            memset(mem_at(c), 0, sizeof(mem_chunk_t));
            mem_owner[c] = (uint8_t)o;
            return (int32_t)c;
        }
    return -1;
}

/* main loop: chunk c back to the pool (after its owner's list has stopped handing it to the ISR) */
static void mem_free(uint32_t c)
{
    if (c < MEM_NC)
        mem_owner[c] = MEM_FREE;
}

/* chunk c changes hands (a WAV's chunks becoming a tape's) */
static void mem_give(uint32_t c, uint32_t o)
{
    if (c < MEM_NC && mem_owner[c] != MEM_FREE)
        mem_owner[c] = (uint8_t)o;
}

/* chunks owner o holds (MEM_FREE: the free ones) */
static uint32_t mem_count(uint32_t o)
{
    uint32_t c, n = 0;
    for (c = 0; c < MEM_NC; c++)
        n += mem_owner[c] == o;
    return n;
}

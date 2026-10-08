/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: the shared core. Four tracks, each a source engine feeding a fixed device chain (GRAIN, RESONATOR,
 * COLOR, SPACE: docs/bryo-architecture.md). This header holds what the kept hardware layer and every Bryo
 * module agree on: the frame sizes, the system state the main loop and the audio ISR share, the ring
 * barrier, the boot guard. Device and modulator state lives with each module. */

#define NTRK 4u                  /* tracks */
#define NWHITE 16u               /* white keys: slices, steps or notes */
#define NBLACK 11u               /* black keys: the global macros */
#define HALF_FRAMES 128          /* I2S half buffer: 2.9 ms at 44.1 kHz */
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

/* ----------------------------------------------------------- system --- */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update */
static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER5 ISR in main.c) */
/* boot-loop guard (main.c): two boots in a row that die in the first 30 s -> UBOOT */
#define BOOTGUARD_MAGIC 0x42475244u
struct { uint32_t magic, failed, pending; } bootguard __attribute__((section(".noinit")));

/* What the main loop and the audio ISR share. The ISR writes cpu_q8; the main loop writes the rest, and the
 * ISR reads them once per control block (a torn read of a 32-bit word cannot happen on this core). */
typedef struct {
    uint32_t cpu_q8;             /* audio ISR load, 1/256 (audio.c) */
    uint32_t master_q12;         /* the MASTER pot, squared: 0 .. ~4096 (main.c) */
    int32_t batt_raw;            /* smoothed ADC ch3 (battery divider), 0 = not read yet */
    uint16_t bpm;                /* global tempo, 40..240 */
    uint8_t playing;             /* PLAY/STOP */
    uint8_t sel;                 /* the focused track 0..NTRK-1 */
    uint8_t keys_live;           /* 1: the white keys play the focused track (0 while SEL picks a track) */
    uint8_t rec;                 /* REC armed, a bit per track (the ISR records a track while its tape is ready) */
    uint8_t keys_grain;          /* 1: the GRAIN page is up: on a TAPE track the white keys move GRAIN's cursor */
} sys_t;
static sys_t sys;

#define MASTER_FULL 4096         /* the MASTER pot's top, Q12 */

/* each track's controls outside its pages: the main loop writes whole bytes, the ISR reads them once per block */
typedef struct {
    volatile uint8_t mute;
    volatile uint8_t octave;     /* the keys' octave (OCT- / OCT+), 1..6: SYNTH's white key 1 is C of octave + 1 */
    volatile uint8_t level;      /* 0..127 */
} track_ctl_t;
static track_ctl_t track[NTRK];

/* --------------------------------------------------------------- keys --- */
/* The 27 keys, F3..G5 (hal/fm1_input.h fm1_in.notes bit 0..26), as Bryo reads them: 16 white keys numbered
 * left to right 0..15 (slices, steps, notes) and 11 black keys numbered 0..10, the global macros of PRD 2.3 in
 * the PRD's order: OP1..OP4 (mutes), OP5 (reverse), OP6 (half speed), PIT, GLO (pitch down, up), MONO, POLY,
 * 0 (freeze). 0xFF: not that colour. */
#define KEY_NONE 0xFFu
static const uint8_t KEY_WHITE[27] = {0, 0xFF, 1, 0xFF, 2, 0xFF, 3, 4, 0xFF, 5, 0xFF, 6, 7, 0xFF, 8, 0xFF, 9, 0xFF, 10,
                                      11, 0xFF, 12, 0xFF, 13, 14, 0xFF, 15};
static const uint8_t KEY_BLACK[27] = {0xFF, 0, 0xFF, 1, 0xFF, 2, 0xFF, 0xFF, 3, 0xFF, 4, 0xFF, 0xFF, 5, 0xFF, 6, 0xFF,
                                      7, 0xFF, 0xFF, 8, 0xFF, 9, 0xFF, 0xFF, 10, 0xFF};
enum { BK_OP1, BK_OP2, BK_OP3, BK_OP4, BK_OP5, BK_OP6, BK_PIT, BK_GLO, BK_MONO, BK_POLY, BK_ZERO };
#define WHITE_MASK 0x05AD5AD5u   /* fm1_in.notes bits of the white keys */
/* fm1_in.notes -> the white keys held, bit k = white key k (a plain loop: no builtin that could become a runtime
 * library call on the target compiler) */
static inline uint32_t white_keys(uint32_t notes)
{
    uint32_t w = 0, n;
    notes &= WHITE_MASK;
    for (n = 0; notes; n++, notes >>= 1)
        if (notes & 1u)
            w |= 1u << KEY_WHITE[n];
    return w;
}

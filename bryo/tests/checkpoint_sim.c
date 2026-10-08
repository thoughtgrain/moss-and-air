/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: the hardware checkpoint (docs/hardware-checkpoint.md) run on the host, as far as the host can.
 *
 * The FM-1's pi32v2 core has no emulator, so this runs the firmware's own sources (tests/bryo_host.c's build, the
 * hardware stubbed) through the run sheet's scenarios instead:
 *
 *   checkpoint_sim wav DIR      each run (1..16) rendered to DIR/runN.wav (stereo, 44.1 kHz) to listen to, with what the
 *                               firmware reports along the way (grains, memory, tape length)
 *   checkpoint_sim run N        run N alone, for a cost measurement (tests/checkpoint_sim.sh runs it under callgrind)
 *   checkpoint_sim stress SECS  the timing the host tests can't show: the audio interrupt and TIMER5's usb_poll as
 *                               real asynchronous signals that cut into the main loop at any instruction (as on the
 *                               device: TIMER5 outranks the audio, and usb_poll never runs nested in it), while the
 *                               main loop turns knobs, changes TRACKS, records, freezes and draws the screen, and a
 *                               WAV after WAV arrives over the drive. The shared memory's books are checked as it
 *                               goes: every chunk owned once, listed where its owner says, none lost.
 *
 * What it can't tell: the FM-1's cycles. Host instructions per sample are a ratio against the engines Felucca already
 * runs on the device, not a load. */
#define _GNU_SOURCE
#include <signal.h>                                      /* (before the firmware's libc shims, which rename a few */
#include <sys/time.h>                                    /*  libc functions) */
#include <time.h>
#include <unistd.h>
#define main bryo_main
#include "bryo_host.c"
#undef main

/* ----------------------------------------------------------------- wav --- */
static int16_t *wav_buf;
static uint32_t wav_n, wav_cap;
static uint32_t wav_clip;

static void wav_block(void)                              /* one control block of the instrument into the take */
{
    uint32_t i;
    chain_block(out, CTL);
    for (i = 0; i < 2u * CTL; i++) {
        int32_t x = out[i];
        if (x > 32767 || x < -32768)
            wav_clip++;
        if (wav_n < wav_cap)
            wav_buf[wav_n++] = (int16_t)clamp(x, -32768, 32767);
    }
}

static void wav_save(const char *path)
{
    FILE *f = fopen(path, "wb");
    uint8_t h[44];
    uint32_t bytes = wav_n * 2u;
    if (!f)
        return;
    memcpy(h, "RIFF", 4);
    wr32(h + 4, 36u + bytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    wr32(h + 16, 16);
    wr16(h + 20, 1);
    wr16(h + 22, 2);
    wr32(h + 24, 44100);
    wr32(h + 28, 44100 * 4);
    wr16(h + 32, 4);
    wr16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    wr32(h + 40, bytes);
    fwrite(h, 1, 44, f);
    fwrite(wav_buf, 2, wav_n, f);
    fclose(f);
}

/* s seconds of the instrument, the main loop's bookkeeping between blocks (as main.c: every block, here) */
static void play(double s)
{
    uint32_t b, nb = (uint32_t)(s * 1378.125);
    for (b = 0; b < nb; b++) {
        wav_block();
        chain_poll();
        vdisk_poll();
    }
}

static void all_grain(int idx, int16_t v)
{
    uint32_t t;
    for (t = 0; t < NTRK; t++)
        tp[t].dev[DEV_GRAIN][idx] = v;
}

/* play s seconds with track t's keys going (a phrase of notes, a new one every 0.25 s, held): a SYNTH or POLY track
 * being played while the rest runs */
static void play_phrase(uint32_t t, double s)
{
    static const uint8_t NOTES[8] = {0, 7, 12, 10, 3, 7, 5, 2};
    uint32_t k, n = (uint32_t)(s * 4);
    sys.sel = (uint8_t)t;
    sys.keys_live = 1;
    for (k = 0; k < n; k++) {
        fm1_in.notes = note_bit_of_white(NOTES[k % 8u]) | (k % 4u == 0u ? note_bit_of_white(NOTES[k % 8u] + 4u) : 0u);
        play(0.25);
    }
    fm1_in.notes = 0;
}

static uint32_t sounding(void)
{
    uint32_t t, n = 0;
    for (t = 0; t < NTRK; t++)
        n += grain_count(t);
    return n;
}

/* the run sheet's runs; returns the seconds rendered. say: what to print after it */
static double run(int n, char *say, size_t sz)
{
    power_on();
    vdisk_mount();
    sys.playing = n != 9;
    switch (n) {
    case 1:                                              /* the baseline: four reels, GRAIN off */
        play(6);
        snprintf(say, sz, "4 reels playing, GRAIN off");
        return 6;
    case 2:                                              /* WET 100 everywhere, the defaults */
        all_grain(GP_WET, 100);
        play(6);
        snprintf(say, sz, "WET 100, defaults: %u grains sounding, buffers %u chunks", sounding(), mem_count(MEM_GRAIN) +
                 mem_count(MEM_GRAIN + 1) + mem_count(MEM_GRAIN + 2) + mem_count(MEM_GRAIN + 3));
        return 6;
    case 3: case 4: case 5: case 6:                      /* the cap, backwards, SCAN TAPE, TRACKS 2 */
        all_grain(GP_WET, 100);
        all_grain(GP_RATE, 100);
        all_grain(GP_SIZE, 500);
        if (n == 4)
            all_grain(GP_REV, 100);
        if (n == 5)
            all_grain(GP_SCAN, SCAN_TAPE);
        if (n == 6)
            chain_tracks(2);
        play(6);
        snprintf(say, sz, "%s: %u grains sounding (%u %u %u %u)", n == 3 ? "the cap" : n == 4 ? "the cap, all backwards"
                 : n == 5 ? "the cap, SCAN TAPE" : "the cap, TRACKS 2", sounding(), grain_count(0), grain_count(1),
                 grain_count(2), grain_count(3));
        return 6;
    case 7: {                                            /* a blank tape recorded for 12 s, then looped */
        uint32_t t;
        for (t = 1; t < NTRK; t++)
            track[t].mute = 1;                           /* (track 1's beat into track 2, track 2 heard after) */
        tape_clear(1);
        tape_prepare(1);
        sys.rec = 2u;
        play(12);
        sys.rec = 0;
        tape_unprepare(1);
        track[0].mute = 1;
        track[1].mute = 0;
        play(8);
        snprintf(say, sz, "track 2 recorded 12 s: its tape %u blocks (%.2f s), %u chunks", tape_ctl[1].nblk,
                 tape_ctl[1].nblk * 256.0 / 22050, tape_ctl[1].nch);
        return 20;
    }
    case 8: {                                            /* the 0 key tapped after 3 s: a 1-bar loop, let go at 9 s */
        uint32_t k;
        tp[0].dev[DEV_GRAIN][GP_WET] = 100;
        track[1].mute = track[2].mute = track[3].mute = 1;
        play(3);
        for (k = 0; k < 27u && KEY_BLACK[k] != BK_ZERO; k++)
            ;
        fm1_in.notes = 1u << k;
        key_edge(k);
        fm1_in.notes = 0;
        ui_input();
        play(6);
        fm1_in.notes = 1u << k;
        key_edge(k);
        fm1_in.notes = 0;
        ui_input();
        play(3);
        snprintf(say, sz, "track 1, the 0 key tapped at 3 s, tapped again at 9 s: frozen %s", sys.freeze ? "still" : "and let go");
        return 12;
    }
    case 9: {                                            /* SYNTH played, transport stopped, GRAIN WET 100 */
        static const uint8_t NOTES[8] = {0, 4, 7, 11, 12, 7, 4, 2};
        uint32_t k;
        tp[0].src = SRC_SYNTH;
        tp[0].dev[DEV_GRAIN][GP_WET] = 70;
        for (k = 0; k < 8u; k++) {
            fm1_in.notes = note_bit_of_white(NOTES[k]);
            play(0.4);
            fm1_in.notes = 0;
            play(0.35);
        }
        play(2);
        snprintf(say, sz, "SYNTH, transport stopped: a phrase, granulated as it's played (%u grains at the end)",
                 grain_count(0));
        return 8;
    }
    case 11: case 12: {                                  /* RESONATOR on all four (12: with GRAIN at its cap too) */
        uint32_t t;
        for (t = 0; t < NTRK; t++) {
            tp[t].dev[DEV_RESO][RP_WET] = 60;
            tp[t].dev[DEV_RESO][RP_PTCH] = (int16_t)(40 + 5 * t);
            tp[t].dev[DEV_RESO][RP_SCAL] = (int16_t)(t & 1u ? 1 : 0);
        }
        if (n == 12) {
            all_grain(GP_WET, 100);
            all_grain(GP_RATE, 100);
            all_grain(GP_SIZE, 500);
        }
        play(6);
        snprintf(say, sz, "RESONATOR WET 60 on all four (HARM and MAJ, four roots)%s: %u strings, %u grains",
                 n == 12 ? " with GRAIN at the cap" : "", reso[0].nch + reso[1].nch + reso[2].nch + reso[3].nch, sounding());
        return 6;
    }
    case 13: case 14: case 15: case 16: {                /* COLOR, SPACE, the full chain, and three tracks as I'd
                                                          * play them */
        uint32_t t, nsp = 0, n8 = 0;
        for (t = 0; t < NTRK; t++) {
            int16_t *c = tp[t].dev[DEV_COLOR], *v = tp[t].dev[DEV_SPACE];
            if (n == 13 || n == 15) {
                c[CP_DRIV] = 60;
                c[CP_CRSH] = 40;
                c[CP_CMOD] = CMOD_BOTH;
                c[CP_NOIS] = 30;
                c[CP_TILT] = 30;
                c[CP_LVL] = -6;
            }
            if (n == 16)
                c[CP_DRIV] = 40;
            if (n >= 14) {
                v[SP_DLY] = 50;
                v[SP_VERB] = 40;
                v[SP_TIME] = (int16_t)(180 + 40 * t);
            }
            if (n == 15) {
                tp[t].dev[DEV_RESO][RP_WET] = 60;
                tp[t].dev[DEV_RESO][RP_PTCH] = (int16_t)(40 + 5 * t);
            }
        }
        if (n == 15) {
            all_grain(GP_WET, 100);
            all_grain(GP_RATE, 100);
            all_grain(GP_SIZE, 500);
        }
        if (n == 16) {
            chain_tracks(3);
            all_grain(GP_WET, 100);
        }
        play(6);
        for (t = 0; t < NTRK; t++) {
            nsp += mem_count(MEM_SPACE + t);
            n8 += space[t].dly.bits8 + space[t].pre.bits8;
        }
        snprintf(say, sz, "%s: SPACE %u chunks (%u lines 8-bit), %u grains, %u strings, %.1f s free",
                 n == 13 ? "COLOR on all four (DRIV 60, CRSH 40 BOTH, NOIS 30, TILT 30)" :
                 n == 14 ? "SPACE on all four (DLY 50, VERB 40)" :
                 n == 15 ? "everything on all four (GRAIN at the cap, RESONATOR, COLOR, SPACE)" :
                           "TRACKS 3, GRAIN WET 100, DRIV 40, SPACE",
                 nsp, n8, sounding(), reso[0].nch + reso[1].nch + reso[2].nch + reso[3].nch,
                 mem_count(MEM_FREE) * 4096.0 / 22050);
        return 6;
    }
    case 17: {                                           /* the mixer: every channel strip on, the compressor */
        uint32_t t;
        for (t = 0; t < NTRK; t++) {
            tp[t].ch[CH_LOW] = 4;
            tp[t].ch[CH_HIGH] = -3;
            tp[t].ch[CH_FILT] = (int16_t)(t & 1u ? 30 : -40);
            tp[t].ch[CH_PAN] = (int16_t)(t * 60 - 90);
        }
        mst[MS_AMT] = 60;
        mst[MS_MIX] = 80;
        play(6);
        snprintf(say, sz, "the mixer: LOW HIGH FILT PAN on all four, the compressor at AMT 60 (taking %.1f dB off)",
                 comp.gr_q8 * 6.02 / 256);
        return 6;
    }
    case 18: {                                           /* realistic: three tracks as I'd play them */
        int16_t *d;
        chain_tracks(3);
        tp[0].src = SRC_SYNTH;                           /* T1: the synth, played, through GRAIN and SPACE */
        tp[0].dev[DEV_GRAIN][GP_WET] = 60;
        tp[0].dev[DEV_SPACE][SP_DLY] = 40;
        tp[0].dev[DEV_SPACE][SP_VERB] = 30;
        d = tp[1].dev[DEV_COLOR];                        /* T2: a reel, driven, a little low end, darker */
        d[CP_DRIV] = 40;
        tp[1].ch[CH_LOW] = 3;
        tp[1].ch[CH_FILT] = -20;
        tp[2].dev[DEV_RESO][RP_WET] = 40;                /* T3: a reel ringing the strings */
        mst[MS_AMT] = 40;
        play_phrase(0, 6);
        snprintf(say, sz, "three tracks: a played synth through GRAIN and SPACE, a driven reel, a reel on RESONATOR, "
                 "the compressor (%u grains)", sounding());
        return 6;
    }
    case 19: case 21: case 22: {                         /* realistic: a four-track groove (21: its busiest moment,
                                                          * 22: bounced onto track 4 while it plays) */
        uint32_t t;
        tp[0].dev[DEV_COLOR][CP_CRSH] = 30;              /* T1 drums: crushed a little */
        tp[1].dev[DEV_GRAIN][GP_WET] = 50;               /* T2: grains */
        tp[1].ch[CH_PAN] = -40;
        tp[2].src = SRC_SYNTH;                           /* T3: a synth bass, filtered */
        tp[2].ch[CH_FILT] = -30;
        tp[3].dev[DEV_SPACE][SP_VERB] = 40;              /* T4: in a room */
        tp[3].ch[CH_PAN] = 40;
        mst[MS_AMT] = 30;
        if (n == 21) {                                   /* a build-up: grains denser, strings, every filter */
            tp[1].dev[DEV_GRAIN][GP_RATE] = 80;
            tp[3].dev[DEV_RESO][RP_WET] = 50;
            tp[0].dev[DEV_SPACE][SP_DLY] = 40;
            for (t = 0; t < NTRK; t++)
                tp[t].ch[CH_FILT] = (int16_t)(t & 1u ? 35 : -45);
        }
        if (n == 22) {                                   /* REC on T4: the others printed onto its tape */
            tp[3].recin = RIN_OTHR;
            tape_clear(3);
            if (tape_prepare(3) > 0)
                sys.rec |= 8u;
        }
        play_phrase(2, 6);
        snprintf(say, sz, "%s (%u grains, %u strings)", n == 19 ? "a four-track groove: crushed drums, grains, a filtered "
                 "synth bass, a reverb, the compressor" : n == 21 ? "the groove's busiest moment: denser grains, strings, "
                 "a delay, every channel filtered" : "the groove, bounced onto track 4 as it plays",
                 sounding(), reso[0].nch + reso[1].nch + reso[2].nch + reso[3].nch);
        return 6;
    }
    case 20: {                                           /* realistic: an ambient pad on two tracks */
        chain_tracks(2);
        tp[0].src = SRC_SYNTH;                           /* T1: a held synth, stretched into a long cloud */
        tp[0].dev[DEV_GRAIN][GP_WET] = 100;
        tp[0].dev[DEV_GRAIN][GP_RATE] = 70;
        tp[0].dev[DEV_GRAIN][GP_SIZE] = 300;
        tp[0].dev[DEV_SPACE][SP_VERB] = 70;
        tp[0].dev[DEV_SPACE][SP_DEC] = 80;
        tp[0].dev[DEV_SPACE][SP_DLY] = 30;
        tp[1].dev[DEV_RESO][RP_WET] = 50;                /* T2: a reel through the strings, in the room */
        tp[1].dev[DEV_SPACE][SP_VERB] = 50;
        mst[MS_AMT] = 20;
        play_phrase(0, 6);
        snprintf(say, sz, "an ambient pad on two tracks: a synth stretched by GRAIN into a long room, a reel through "
                 "the strings (%u grains)", sounding());
        return 6;
    }
    default: {                                           /* a 20 s WAV over TAPE3.WAV while the reels play */
        static uint8_t w[20 * 22050 * 2 + 4096];
        uint32_t len = make_wav(w, 22050, 1, 16, 1, 22050 * 20, 330.0), k, lba = VD_DATA + 3000u * VD_SPC;
        for (k = 0; k < (len + 511u) / 512u; k++) {      /* a sector each 2 audio blocks (~250 KB/s: USB's pace) */
            uint8_t sec[512];
            memset(sec, 0, sizeof sec);
            memcpy(sec, w + k * 512u, len - k * 512u < 512u ? len - k * 512u : 512u);
            vdisk_write(lba + k, sec);
            play(2.0 / 1378.125);
        }
        cap.named = 1;                                   /* (the directory entry: TAPE3.WAV) */
        cap.dest = 2;
        play(2);
        snprintf(say, sz, "a 20 s WAV over TAPE3.WAV while playing: track 3's tape %u blocks (%.2f s); %s",
                 tape_ctl[2].nblk, tape_ctl[2].nblk * 256.0 / 22050, ui.msg);
        return 20.0 + 2.0 + 0.0;
    }
    }
}

/* -------------------------------------------------------------- stress --- */
static volatile sig_atomic_t st_audio_on, st_usb_on;
static volatile uint32_t st_blocks, st_sectors;
static uint8_t *st_wav;
static uint32_t st_len, st_pos, st_lba;

static void on_audio(int sig)                            /* the audio interrupt (ALNK0): one control block */
{
    (void)sig;
    if (!st_audio_on)
        return;
    chain_block(out, CTL);
    st_blocks++;
}

static void on_timer5(int sig)                           /* TIMER5's usb_poll: the drive takes a sector, and in the
                                                          * record mode the computer's packet (EP4 OUT) arrives */
{
    uint8_t sec[512];
    (void)sig;
    if (st_usb_on && ur.state != UR_OFF) {
        static uint8_t pk[45 * 4];
        static uint32_t n;
        uint32_t i;
        for (i = 0; i < sizeof pk; i++)
            pk[i] = (uint8_t)(n * 7u + i * 13u);
        uaco_frames(pk, ++n % 10u ? 44u : 45u);
    }
    if (!st_usb_on || !vd.ready)
        return;
    if (st_pos >= st_len) {                              /* (a WAV done: the next one starts after it's placed) */
        if (cap.on)
            return;
        st_pos = 0;
        st_lba = st_lba == VD_DATA + 3000u * VD_SPC ? VD_DATA + 3600u * VD_SPC : VD_DATA + 3000u * VD_SPC;
    }
    memset(sec, 0, sizeof sec);
    memcpy(sec, st_wav + st_pos, st_len - st_pos < 512u ? st_len - st_pos : 512u);
    vdisk_write(st_lba + st_pos / 512u, sec);
    st_pos += 512u;
    st_sectors++;
}

static sigset_t st_both;
static void st_block(int on) { sigprocmask(on ? SIG_BLOCK : SIG_UNBLOCK, &st_both, 0); }

/* the shared memory's books, with both interrupts held off: returns the number of problems (printed) */
static uint32_t books(void)
{
    static uint8_t seen[MEM_NC];
    uint32_t bad = 0, t, k, c;
    st_block(1);
    memset(seen, 0, sizeof seen);
#define SEE(ch, own, what)                                                                         \
    do {                                                                                           \
        uint32_t c_ = (ch);                                                                        \
        if (c_ >= MEM_NC || seen[c_]++ || mem_owner[c_] != (own)) {                                \
            if (bad++ < 8)                                                                         \
                printf("checkpoint: chunk %u (%s): listed twice or owner %u\n", c_, what,          \
                       c_ < MEM_NC ? mem_owner[c_] : 999u);                                        \
        }                                                                                          \
    } while (0)
    for (t = 0; t < NTRK; t++) {
        for (k = 0; k < tape_ctl[t].nch; k++)
            SEE(tape_ctl[t].map[k], MEM_TAPE + t, "a tape");
        for (k = 0; k < gbuf[t].nch; k++)
            SEE(gbuf[t].map[k], MEM_GRAIN + t, "a GRAIN buffer");
        for (k = 0; k < reso[t].nch; k++)
            SEE(reso[t].map[k], MEM_RESO + t, "a string");
        for (k = 0; k < space[t].dly.nch; k++)
            SEE(space[t].dly.map[k], MEM_SPACE + t, "the delay's line");
        for (k = 0; k < space[t].pre.nch; k++)
            SEE(space[t].pre.map[k], MEM_SPACE + t, "the pre-delay's line");
        for (k = 0; k < space[t].rev.nch; k++)
            SEE(space[t].rev.map[k], MEM_SPACE + t, "the room");
        if (tape_ctl[t].nblk > tape_ctl[t].nch * MEM_CB && !tape_ctl[t].empty)
            if (bad++ < 8)
                printf("checkpoint: track %u's tape longer (%u blocks) than its chunks (%u)\n", t, tape_ctl[t].nblk,
                       tape_ctl[t].nch);
    }
    for (k = 0; k < capm.nch; k++)
        SEE(capm.map[k], MEM_IMPORT, "the capture");
    for (k = 0; k < ur.nch; k++)
        SEE(ur.map[k], MEM_IMPORT, "the USB take");
    for (k = vd_sp_r; k != vd_sp_w; k++)
        SEE(vd_spare[k % VD_NSPARE], MEM_SPARE, "a spare");
    for (c = 0; c < MEM_NC; c++)
        if (mem_owner[c] != MEM_FREE && !seen[c] && bad++ < 8)
            printf("checkpoint: chunk %u owned (%u) but nobody lists it: lost\n", c, mem_owner[c]);
#undef SEE
    st_block(0);
    return bad;
}

static uint32_t st_rng = 12345;
static uint32_t st_rand(uint32_t n) { st_rng = st_rng * 1664525u + 1013904223u; return (st_rng >> 8) % n; }

static int stress(double secs)
{
    struct sigaction sa;
    struct itimerval it;
    timer_t t5;
    struct sigevent ev;
    struct itimerspec its;
    static uint8_t w[6 * 22050 * 2 + 4096];
    uint32_t bad = 0, checks = 0, actions = 0, places = 0, takes = 0;
    double t0;
    struct timespec ts;
    power_on();
    vdisk_mount();
    st_wav = w;
    st_len = make_wav(w, 22050, 1, 16, 1, 22050 * 6, 440.0);   /* (6 s WAVs, one after another) */
    st_pos = 0;
    st_lba = VD_DATA + 3000u * VD_SPC;
    sigemptyset(&st_both);
    sigaddset(&st_both, SIGALRM);
    sigaddset(&st_both, SIGRTMIN);
    memset(&sa, 0, sizeof sa);                           /* the audio: TIMER5's usb_poll can't run inside it */
    sa.sa_handler = on_audio;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGRTMIN);
    sigaction(SIGALRM, &sa, 0);
    sa.sa_handler = on_timer5;                           /* TIMER5 outranks the audio: nothing cuts into it */
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGALRM);
    sigaction(SIGRTMIN, &sa, 0);
    memset(&ev, 0, sizeof ev);
    ev.sigev_notify = SIGEV_SIGNAL;
    ev.sigev_signo = SIGRTMIN;
    timer_create(CLOCK_MONOTONIC, &ev, &t5);
    sys.playing = 1;
    st_audio_on = st_usb_on = 1;
    it.it_interval.tv_sec = it.it_value.tv_sec = 0;
    it.it_interval.tv_usec = it.it_value.tv_usec = getenv("CHECKPOINT_SLOW") ? 1500 : 150;   /* (faster than the
                                                         * device: more cuts per second; SLOW for a sanitizer build) */
    setitimer(ITIMER_REAL, &it, 0);
    its.it_interval.tv_sec = its.it_value.tv_sec = 0;
    its.it_interval.tv_nsec = its.it_value.tv_nsec = getenv("CHECKPOINT_SLOW") ? 2300000 : 230000;
    timer_settime(t5, 0, &its, 0);
    clock_gettime(CLOCK_MONOTONIC, &ts);
    t0 = ts.tv_sec + ts.tv_nsec * 1e-9;
    for (;;) {
        double now;
        uint32_t a = st_rand(100);
        if (ur.state != UR_OFF && a < 66)                /* (in the record mode only it and the knobs answer) */
            a = 66 + st_rand(8);
        clock_gettime(CLOCK_MONOTONIC, &ts);
        now = ts.tv_sec + ts.tv_nsec * 1e-9;
        if (now - t0 > secs)
            break;
        if (a < 25) {                                    /* a knob on a GRAIN page (WET, RATE, SIZE, SCAN, FDBK) */
            static const uint8_t K[5] = {GP_WET, GP_RATE, GP_SIZE, GP_SCAN, GP_FDBK};
            uint32_t t = st_rand(NTRK), k = K[st_rand(5)];
            const pdesc_t *d = &DEV_P[DEV_GRAIN][k];
            tp[t].dev[DEV_GRAIN][k] = (int16_t)(d->min + (int32_t)st_rand((uint32_t)(d->max - d->min + 1)));
        } else if (a < 30) {                             /* TRACKS (GLO held + SELECT) */
            chain_tracks(1 + (int32_t)st_rand(NTRK));
        } else if (a < 40) {                             /* REC on a track: on (a blank or its own tape) or off */
            uint32_t t = st_rand(NTRK);
            if ((sys.rec >> t) & 1u) {
                sys.rec &= (uint8_t)~(1u << t);
                tape_unprepare(t);
            } else if (t < sys.ntrk) {
                if (st_rand(3) == 0)
                    tape_clear(t);
                if (tape_prepare(t) > 0)
                    sys.rec |= (uint8_t)(1u << t);
            }
        } else if (a < 43) {                             /* the 0 key: the freeze latched or let go */
            sys.freeze = (uint8_t)!sys.freeze;
        } else if (a < 46) {                             /* a source switched */
            tp[st_rand(NTRK)].src = (uint8_t)st_rand(NSRC);
        } else if (a < 48) {                             /* the tempo */
            sys.bpm = (uint16_t)(60 + st_rand(140));
        } else if (a < 50) {                             /* a clear */
            tape_clear(st_rand(NTRK));
        } else if (a < 62) {                             /* RESONATOR's WET, SPACE's DLY, VERB, TIME, PRE (memory */
            static const uint8_t K[5] = {RP_WET, SP_DLY, SP_VERB, SP_TIME, SP_PRE};   /* taken and given back) */
            uint32_t t = st_rand(NTRK), k = st_rand(5), dv = k ? DEV_SPACE : DEV_RESO;
            const pdesc_t *d = &DEV_P[dv][K[k]];
            int32_t x = d->min + (int32_t)st_rand((uint32_t)(d->max - d->min + 1));
            tp[t].dev[dv][K[k]] = (int16_t)(st_rand(3) == 0 ? d->min : x);
        } else if (a < 70) {                             /* the USB record mode: in, a take, onto a track, out */
            if (ur.state == UR_OFF) {
                if (!sys.usbrec && !ur.isr_in) {
                    sys.playing = 0;
                    usbrec_enter();
                    ui.view = VIEW_USBREC;
                }
            } else if (ur.state == UR_READY) {
                if (st_rand(3))
                    usbrec_rec();
                else
                    usbrec_back();
            } else if (ur.state == UR_RECORDING) {
                if (st_rand(4) == 0)
                    usbrec_rec();
            } else {
                usbrec_pick(st_rand(NTRK));
                usbrec_knob(st_rand(4), (int32_t)st_rand(9) - 4);   /* (trimmed, shaped, while it previews) */
                if (st_rand(4)) {
                    takes += ur.dest >= 0;
                    usbrec_rec();
                } else {
                    usbrec_back();
                }
            }
            if (ur.state == UR_OFF)
                ui.view = VIEW_PAGE;
        } else if (a < 77) {                             /* the mixer: a channel knob, the compressor */
            uint32_t t = st_rand(NTRK), k = st_rand(NCH + NMS);
            const pdesc_t *d = k < NCH ? &CH_P[k] : &MS_P[k - NCH];
            int16_t x = (int16_t)(d->min + (int32_t)st_rand((uint32_t)(d->max - d->min + 1)));
            if (k < NCH)
                tp[t].ch[k] = x;
            else
                mst[k - NCH] = x;
        } else if (a < 81) {                             /* a COLOR knob */
            uint32_t t = st_rand(NTRK), k = st_rand(9);
            const pdesc_t *d = &DEV_P[DEV_COLOR][k];
            tp[t].dev[DEV_COLOR][k] = (int16_t)(d->min + (int32_t)st_rand((uint32_t)(d->max - d->min + 1)));
        }
        actions++;
        if (cap.on && cap.done && !cap.named) {          /* (the computer writes the WAV's directory entry) */
            st_block(1);
            cap.named = 1;
            cap.dest = (int8_t)st_rand(NTRK);
            st_block(0);
        }
        {
            int placing = cap.on && cap.done && cap.named;
            chain_poll();
            vdisk_poll();
            places += placing && !cap.on;               /* (a WAV placed on a track) */
        }
        ui_input();
        if (st_rand(8) == 0) {
            ui.force = 1;
            ui_draw();
        }
        if (st_rand(16) == 0) {
            bad += books();
            checks++;
        }
        usleep(st_rand(400));
    }
    st_audio_on = st_usb_on = 0;
    it.it_interval.tv_usec = it.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &it, 0);
    timer_delete(t5);
    bad += books();
    printf("checkpoint: stress %.0f s: %u audio blocks and %u USB sectors cut into the main loop, %u actions, %u "
           "book checks, %u WAVs placed, %u USB takes put on tracks: %s\n", secs, st_blocks, st_sectors, actions,
           checks + 1u, places, takes, bad ? "PROBLEMS" : "the books balance");
    return bad != 0;
}

int main(int argc, char **argv)
{
    char say[200];
    if (argc > 2 && !strcmp(argv[1], "run")) {
        int n = atoi(argv[2]);
        wav_cap = 0;
        run(n, say, sizeof say);
        printf("checkpoint: run %d: %s\n", n, say);
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "stress"))
        return stress(atof(argv[2]));
    if (argc > 2 && !strcmp(argv[1], "wav")) {
        int n;
        wav_cap = 23u * 44100u * 2u;
        wav_buf = malloc(wav_cap * sizeof *wav_buf);
        for (n = 1; n <= 22; n++) {
            char path[512];
            wav_n = wav_clip = 0;
            run(n, say, sizeof say);
            snprintf(path, sizeof path, "%s/run%d.wav", argv[2], n);
            wav_save(path);
            printf("checkpoint: run %2d (%5.1f s, %u samples past full scale): %s\n", n, wav_n / 88200.0, wav_clip, say);
        }
        return 0;
    }
    fprintf(stderr, "checkpoint_sim wav DIR | run N | stress SECS\n");
    return 2;
}

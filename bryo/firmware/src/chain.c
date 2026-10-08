/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: the four tracks, rendered by the audio ISR one control block (CTL samples) at a time.
 *
 * Each track starts with its source (source.c): its TAPE (tape.c: the loop plays while the transport runs, the
 * white keys play its 16 slices), SYNTH or POLY (the white keys play notes). REC records onto the tape either way.
 * Then GRAIN (grain.c: grains of a live buffer of the source, or of the tape, blended by WET), from which the track
 * is stereo. Then RESONATOR (reso.c: four tuned strings the track rings through), COLOR (color.c: drive, crush,
 * noise, tilt) and SPACE (space.c: the delay and the room). Then the track's channel strip, its level and its pan
 * (mixer.c), and the four tracks' sum through the master compressor (mixer.c) to the output stage (master.c).
 *
 * What REC records is each track's REC IN (param.c RIN_*, the routing view): by default (AUTO) the other three
 * tracks' mix on a TAPE track and its own source on a SYNTH or POLY track; or the others' mix, one chosen track, or
 * itself (SELF). A track is heard after its devices, level and mute, from the previous control block (0.7 ms late),
 * so tracks can record each other in a chain (T1 into T2 into T3...) or round in a loop.
 *
 * Keys: the ISR reads the debounced white keys itself (fm1_in.notes, updated by the 10 kHz scan) once per block,
 * so a slice starts within one block instead of waiting for a UI frame; they play the focused track (sys.sel)
 * while sys.keys_live. Main loop -> ISR: the main loop owns `.mute`, `.octave` and `.level` and writes whole
 * bytes; the ISR reads them once per block. The ISR owns everything in track_rt_t. */

typedef struct {                     /* ISR only */
    int32_t peak;                    /* |output| peak, decaying (the UI's meter) */
    int32_t act;                     /* the track switched on (TRACKS), Q15: 2 ms ramps; a parked track at 0 isn't
                                      * rendered at all (its heads wait where they were) */
    int32_t last[CTL];               /* the last block's output, after level and mute (what REC hears) */
    int32_t src_g[NSRC];             /* each source's share of the track, Q15: 2 ms ramps when the source changes */
} track_rt_t;

static track_rt_t track_rt[NTRK];
static uint32_t chain_shed_count;

static void chain_init(void)
{
    uint32_t t;
    for (t = 0; t < NTRK; t++) {
        track[t].level = 100;
        track[t].octave = 3;
        track_rt[t].src_g[SRC_TAPE] = 32767;
        track_rt[t].act = 32767;
    }
    tape_init();
}

/* one track's source into s: the chosen one, and the one before it while it fades out. The tape renders last, so
 * REC on a track whose source isn't TAPE can print that source onto it (rin: the other tracks, for a TAPE track;
 * 0: REC isn't armed) */
static void chain_source(uint32_t t, uint32_t keys, const int32_t *rin, int32_t *s, uint32_t n)
{
    track_rt_t *rt = &track_rt[t];
    uint32_t cur = tp[t].src % NSRC, e, i;
    int32_t o[CTL];
    for (i = 0; i < n; i++)
        s[i] = 0;
    for (e = NSRC; e-- > 0;) {
        int32_t g0 = rt->src_g[e], g1 = clamp(g0 + (e == cur ? 1 : -1) * 372 * (int32_t)n, 0, 32767);
        const int32_t *rec = cur == SRC_TAPE || tp[t].recin != RIN_AUTO ? rin : rin ? s : 0;   /* (for the tape) */
        if (e != cur && !g0) {                          /* not heard: only the tape, printing */
            if (e == SRC_TAPE && rec)
                SOURCES[e].render(t, 0, rec, o, n);
            continue;
        }
        SOURCES[e].render(t, e == cur ? keys : 0u, e == SRC_TAPE ? rec : 0, o, n);
        rt->src_g[e] = g1;
        for (i = 0; i < n; i++)                         /* (in 32 bits: POLY's four voices sum past 16) */
            s[i] += ((o[i] >> 2) * ((g0 + (((g1 - g0) * (int32_t)i) >> CTL_LOG2)) >> 1)) >> 12;
    }
}

/* audio ISR: one control block of the whole instrument, interleaved stereo Q15 into out. In the USB record mode
 * (usbrec.c) the computer's sound instead: the tracks fade out over one block as it opens, and come back once the
 * monitor has faded out as it closes. */
static void chain_block(int32_t *out, uint32_t n)
{
    int32_t l[CTL], r[CTL], rin[NTRK][CTL];
    uint32_t i, t, u, keys = sys.keys_live ? white_keys(fm1_in.notes) : 0u, sel = sys.sel, zero = 0;
    uint32_t ntrk = sys.ntrk >= 1u && sys.ntrk <= NTRK ? sys.ntrk : NTRK;
    if (ur.isr_in) {                                    /* the record mode: the monitor */
        usbrec_block(l, r, n);
        master_block(l, r, out, n);
        return;
    }
    gr_plan();                                         /* the grains each track may sound this block (grain.c) */
    for (u = 0; u < 27u; u++)                          /* the 0 black key held, or tapped (latched): GRAIN frozen */
        if (KEY_BLACK[u] == BK_ZERO)
            zero = ((fm1_in.notes >> u) & 1u) | (sys.freeze != 0);
    for (t = 0; t < NTRK; t++)                         /* what each track's REC hears (REC IN), last block */
        if ((sys.rec >> t) & 1u) {
            uint32_t from = tp[t].recin;
            for (i = 0; i < n; i++) {
                int32_t a = 0;
                if (from >= RIN_T1 && from < NRIN)     /* one track (its own: itself) */
                    a = track_rt[from - RIN_T1].last[i];
                else                                   /* AUTO, OTHR: the others' mix */
                    for (u = 0; u < NTRK; u++)
                        if (u != t)
                            a += track_rt[u].last[i];
                rin[t][i] = clamp(a, -32767, 32767);
            }
        }
    for (i = 0; i < n; i++)
        l[i] = r[i] = 0;
    for (t = 0; t < NTRK; t++) {
        const track_ctl_t *c = &track[t];
        track_rt_t *rt = &track_rt[t];
        int32_t s[CTL], gl[CTL], gr[CTL], g = c->mute ? 0 : (int32_t)LEVEL_Q12[c->level & 127u], pk = rt->peak;
        int32_t a0 = rt->act, a1 = clamp(a0 + (t < ntrk ? 1 : -1) * 372 * (int32_t)n, 0, 32767);
        uint32_t k = t == sel ? keys : 0u, gk = 0, rk = 0;
        rt->act = a1;
        if (!a0 && !a1) {                               /* parked (TRACKS): silent, not rendered */
            grain_kill(t);                              /* (its grain slots go to the tracks still on) */
            for (i = 0; i < n; i++)
                rt->last[i] = 0;
            rt->peak = pk - (pk >> 6);
            continue;
        }
        if (k && sys.keys_grain && tp[t].src == SRC_TAPE) {   /* the GRAIN page: the keys move GRAIN's cursor */
            gk = k;
            k = 0;
        }
        if (k && sys.keys_reso) {                       /* the RESONATOR page: the keys set its root; on a TAPE track
                                                         * they pluck it, on SYNTH or POLY they still play the source */
            rk = k;
            if (tp[t].src == SRC_TAPE)
                k = 0;
        }
        chain_source(t, k, (sys.rec >> t) & 1u ? rin[t] : 0, s, n);
        grain_block(t, s, gk, (int)zero, gl, gr, n);    /* from here the track is stereo */
        reso_block(t, gl, gr, rk, tp[t].src == SRC_TAPE, n);
        color_block(t, gl, gr, n);
        space_block(t, gl, gr, n);
        if (a0 != 32767 || a1 != 32767)                  /* switching on or off: the 2 ms ramp */
            for (i = 0; i < n; i++) {
                int32_t a = a0 + (((a1 - a0) * (int32_t)i) >> CTL_LOG2);
                gl[i] = (gl[i] >> 3) * (a >> 3) >> 9;
                gr[i] = (gr[i] >> 3) * (a >> 3) >> 9;
            }
        mix_channel(t, gl, gr, n);                      /* the channel strip: LOW HIGH FILT (mixer.c) */
        if (mix_pan(t, gl, gr, g, n))                   /* the level and the pan (centred: the level below) */
            g = 4096;
        for (i = 0; i < n; i++) {
            int32_t vl = (gl[i] * g) >> 12, vr = (gr[i] * g) >> 12, a = vl < 0 ? -vl : vl, b = vr < 0 ? -vr : vr;
            rt->last[i] = (vl + vr) / 2;                /* (what the other tracks' REC hears: mono) */
            l[i] += vl;
            r[i] += vr;
            if (a > pk)
                pk = a;
            if (b > pk)
                pk = b;
        }
        rt->peak = pk - (pk >> 6);                    /* ~45 ms decay at one block per 0.73 ms */
    }
    grain_recover();
    reso_recover();
    mix_comp(l, r, n);                                  /* the master compressor on the sum (mixer.c) */
    master_block(l, r, out, n);
    if (sys.usbrec) {                                   /* the record mode opening: this block fades the tracks out */
        for (i = 0; i < n; i++) {
            int32_t g = (int32_t)(n - i) * 32767 / (int32_t)n;
            out[2u * i] = (out[2u * i] >> 1) * g >> 14;
            out[2u * i + 1u] = (out[2u * i + 1u] >> 1) * g >> 14;
        }
        ur.isr_in = 1;
    }
}

/* audio ISR, the half after two overloaded halves: take load away. GRAIN's sounding cap goes first (grain.c), down
 * to half; then RESONATOR's strings (reso.c), down to two; the tape is never shed. */
static void chain_shed(void)
{
    chain_shed_count++;
    if (grain_cap > 4u)                                 /* grains first, then strings */
        grain_shed();
    else
        reso_shed();
}

/* main loop: TRACKS set to n (GLO held + SELECT). The tracks above it park: they fade out in 2 ms and stop
 * rendering (the CPU they took is free), REC on them is let go, and their tapes are the first taken when memory runs
 * short (tape.c tape_steal); nothing is erased, so raising TRACKS again brings back what's still there. */
static void chain_tracks(int32_t n)
{
    uint32_t t;
    n = clamp(n, 1, (int32_t)NTRK);
    sys.ntrk = (uint8_t)n;
    for (t = (uint32_t)n; t < NTRK; t++)
        if ((sys.rec >> t) & 1u) {
            sys.rec &= (uint8_t)~(1u << t);
            tape_unprepare(t);
        }
    if (sys.sel >= (uint32_t)n)
        sys.sel = (uint8_t)(n - 1);
}

/* main loop, every pass: the memory's bookkeeping (tape.c tape_poll: growing tapes and their ends; grain.c
 * grain_poll: GRAIN's buffers; reso.c reso_poll: the strings; space.c space_poll: the delay's and room's lines) */
static void chain_poll(void)
{
    tape_poll();
    grain_poll();
    reso_poll();
    space_poll();
    usbrec_poll();
}

/* all sound off now (an update starting, a panic): the keys stop and the transport stops (the heads fade out) */
static void chain_panic(void)
{
    sys.keys_live = 0;
    sys.playing = 0;
}

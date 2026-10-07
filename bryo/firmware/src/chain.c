/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: the four tracks, rendered by the audio ISR one control block (CTL samples) at a time.
 *
 * Each track starts with its source (source.c): its TAPE (tape.c: the loop plays while the transport runs, the
 * white keys play its 16 slices) or SYNTH (synth.c: the white keys play notes). REC records onto the tape either
 * way. The devices after the source (GRAIN, RESONATOR, COLOR, SPACE) arrive in the next phases; chain_block()
 * keeps the shape they slot into.
 *
 * What REC records, for now: the other three tracks' mix, from the previous control block (0.7 ms late), so a
 * track can print the others onto its tape. Phase 6's ALGORITHM routing makes that a choice; the track's own
 * chain output joins when the devices exist (its dry tape left out, so nothing doubles).
 *
 * Keys: the ISR reads the debounced white keys itself (fm1_in.notes, updated by the 10 kHz scan) once per block,
 * so a slice starts within one block instead of waiting for a UI frame; they play the focused track (sys.sel)
 * while sys.keys_live. Main loop -> ISR: the main loop owns `.mute`, `.octave` and `.level` and writes whole
 * bytes; the ISR reads them once per block. The ISR owns everything in track_rt_t. */

typedef struct {                     /* ISR only */
    int32_t peak;                    /* |output| peak, decaying (the UI's meter) */
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
        const int32_t *rec = cur == SRC_TAPE ? rin : rin ? s : 0;   /* (for the tape) */
        if (e != cur && !g0) {                          /* not heard: only the tape, printing */
            if (e == SRC_TAPE && rec)
                SOURCES[e].render(t, 0, rec, o, n);
            continue;
        }
        SOURCES[e].render(t, e == cur ? keys : 0u, e == SRC_TAPE ? rec : 0, o, n);
        rt->src_g[e] = g1;
        for (i = 0; i < n; i++)
            s[i] += (o[i] * (g0 + (((g1 - g0) * (int32_t)i) >> CTL_LOG2))) >> 15;
    }
}

/* audio ISR: one control block of the whole instrument, interleaved stereo Q15 into out */
static void chain_block(int32_t *out, uint32_t n)
{
    int32_t l[CTL], r[CTL], rin[NTRK][CTL];
    uint32_t i, t, u, keys = sys.keys_live ? white_keys(fm1_in.notes) : 0u, sel = sys.sel;
    for (t = 0; t < NTRK; t++)                         /* what each track's REC hears: the others, last block */
        if ((sys.rec >> t) & 1u)
            for (i = 0; i < n; i++) {
                int32_t a = 0;
                for (u = 0; u < NTRK; u++)
                    if (u != t)
                        a += track_rt[u].last[i];
                rin[t][i] = clamp(a, -32767, 32767);
            }
    for (i = 0; i < n; i++)
        l[i] = r[i] = 0;
    for (t = 0; t < NTRK; t++) {
        const track_ctl_t *c = &track[t];
        track_rt_t *rt = &track_rt[t];
        int32_t s[CTL], g = c->mute ? 0 : (int32_t)LEVEL_Q12[c->level & 127u], pk = rt->peak;
        chain_source(t, t == sel ? keys : 0u, (sys.rec >> t) & 1u ? rin[t] : 0, s, n);
        for (i = 0; i < n; i++) {
            int32_t v = (s[i] * g) >> 12, a = v < 0 ? -v : v;
            rt->last[i] = v;
            l[i] += v;
            r[i] += v;
            if (a > pk)
                pk = a;
        }
        rt->peak = pk - (pk >> 6);                    /* ~45 ms decay at one block per 0.73 ms */
    }
    master_block(l, r, out, n);
}

/* audio ISR, the half after two overloaded halves: take load away. The tape is never shed; GRAIN's sounding cap
 * and RESONATOR's strings will be (phases 3b, 4). */
static void chain_shed(void)
{
    chain_shed_count++;
}

/* all sound off now (an update starting, a panic): the keys stop and the transport stops (the heads fade out) */
static void chain_panic(void)
{
    sys.keys_live = 0;
    sys.playing = 0;
}

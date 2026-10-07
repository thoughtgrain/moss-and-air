/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: the four tracks, rendered by the audio ISR one control block (CTL samples) at a time.
 *
 * Phase 1 (docs/bryo-architecture.md): each track's source is a test voice, a sine per held white key with a
 * short click-free envelope, so the whole path (keys -> main loop -> audio ISR -> master -> DAC and USB) can be
 * checked on the device before the real devices exist. The interface is the one the real chain keeps:
 * chain_block() renders the mix, chain_shed() takes load away when the ISR runs late.
 *
 * Keys: the ISR reads the debounced white keys itself (fm1_in.notes, updated by the 10 kHz scan) once per block,
 * so a key sounds within one block instead of waiting for a UI frame; they play the focused track (sys.sel)
 * while sys.keys_live. Main loop -> ISR: the main loop owns `.mute`, `.octave` and `.level` and writes whole
 * bytes; the ISR reads them once per block. The ISR owns everything in track_rt_t. */

#define TEST_VOICES 4u               /* sines per track (the first four held keys) */
#define TEST_ATK 64u                 /* envelope step per sample, Q15: ~11 ms attack */
#define TEST_REL 24u                 /* ~31 ms release */

typedef struct {                     /* main loop writes, ISR reads */
    volatile uint8_t mute;
    volatile uint8_t octave;         /* the base octave of key 0 (OCT- / OCT+), MIDI octave 2..7 */
    volatile uint8_t level;          /* 0..127 */
} track_ctl_t;

typedef struct {                     /* ISR only */
    uint32_t ph[TEST_VOICES];
    int32_t env[TEST_VOICES];
    uint8_t key[TEST_VOICES];        /* the key each voice plays, 0xFF free */
    int32_t peak;                    /* |output| peak, decaying (the UI's meter) */
} track_rt_t;

static track_ctl_t track[NTRK];
static track_rt_t track_rt[NTRK];
static uint32_t chain_shed_count;

/* white key k (0..15, F3..G5 on the panel) -> semitones above the octave's C: F G A B C D E F G A B C D E F G */
static const uint8_t WHITE_SEMI[NWHITE] = {5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24, 26, 28, 29, 31};

static void chain_init(void)
{
    uint32_t t, v;
    for (t = 0; t < NTRK; t++) {
        track[t].level = 100;
        track[t].octave = 3;
        for (v = 0; v < TEST_VOICES; v++)
            track_rt[t].key[v] = 0xFFu;
    }
}

/* the held keys -> voices: a held key keeps its voice, a new key takes a free one, a released key's voice
 * fades (its key stays until the envelope reaches 0, so the pitch doesn't jump during the release) */
static void test_voices_assign(track_rt_t *r, uint32_t held)
{
    uint32_t v, k;
    for (v = 0; v < TEST_VOICES; v++)
        if (r->key[v] != 0xFFu && ((held >> r->key[v]) & 1u))
            held &= ~(1u << r->key[v]);           /* already sounding */
    for (k = 0; k < NWHITE && held; k++) {
        if (!((held >> k) & 1u))
            continue;
        for (v = 0; v < TEST_VOICES; v++)
            if (r->key[v] == 0xFFu) {
                r->key[v] = (uint8_t)k;
                r->ph[v] = 0;
                r->env[v] = 0;
                break;
            }
        held &= ~(1u << k);
    }
}

static void track_block(uint32_t t, uint32_t held, int32_t *l, int32_t *r, uint32_t n)
{
    const track_ctl_t *c = &track[t];
    track_rt_t *rt = &track_rt[t];
    uint32_t v, i;
    int32_t g = c->mute ? 0 : (int32_t)LEVEL_Q12[c->level & 127u], pk = rt->peak;
    test_voices_assign(rt, held);
    for (v = 0; v < TEST_VOICES; v++) {
        uint32_t k = rt->key[v], inc, ph;
        int32_t e, on;
        if (k == 0xFFu)
            continue;
        on = (int32_t)((held >> k) & 1u);
        inc = pitch_inc((uint32_t)(12u * (c->octave + 1u) + WHITE_SEMI[k]) * 16u);
        ph = rt->ph[v];
        e = rt->env[v];
        for (i = 0; i < n; i++) {
            int32_t s;
            e = on ? (e + (int32_t)TEST_ATK > 32767 ? 32767 : e + (int32_t)TEST_ATK)
                   : (e > (int32_t)TEST_REL ? e - (int32_t)TEST_REL : 0);
            s = (sine_i(ph) * e) >> 17;           /* a quarter of full scale per voice */
            s = (s * g) >> 12;
            l[i] += s;
            r[i] += s;
            ph += inc;
        }
        rt->ph[v] = ph;
        rt->env[v] = e;
        if (!on && !e)
            rt->key[v] = 0xFFu;                   /* released and silent: free */
    }
    for (i = 0; i < n; i++) {
        int32_t a = l[i] < 0 ? -l[i] : l[i];
        if (a > pk)
            pk = a;
    }
    rt->peak = pk - (pk >> 6);                    /* ~45 ms decay at one block per 0.73 ms */
}

/* audio ISR: one control block of the whole instrument, interleaved stereo Q15 into out */
static void chain_block(int32_t *out, uint32_t n)
{
    int32_t l[CTL], r[CTL];
    uint32_t i, t, keys = sys.keys_live ? white_keys(fm1_in.notes) : 0u, sel = sys.sel;
    for (i = 0; i < n; i++)
        l[i] = r[i] = 0;
    for (t = 0; t < NTRK; t++) {
        int32_t tl[CTL], tr[CTL];
        for (i = 0; i < n; i++)
            tl[i] = tr[i] = 0;
        track_block(t, t == sel ? keys : 0u, tl, tr, n);
        for (i = 0; i < n; i++) {
            l[i] += tl[i];
            r[i] += tr[i];
        }
    }
    master_block(l, r, out, n);
}

/* audio ISR, the half after two overloaded halves: take load away. Phase 1 has nothing to shed but the
 * newest test voice; the real chain sheds grains first, then RESONATOR strings, never the tape. */
static void chain_shed(void)
{
    uint32_t t, v;
    for (t = NTRK; t-- > 0;)
        for (v = TEST_VOICES; v-- > 0;)
            if (track_rt[t].key[v] != 0xFFu) {
                track_rt[t].env[v] = 0;
                track_rt[t].key[v] = 0xFFu;
                chain_shed_count++;
                return;
            }
}

/* all sound off now (an update starting, a panic): no key plays until the main loop turns them back on, and the
 * voices still sounding fade in their release */
static void chain_panic(void)
{
    sys.keys_live = 0;
}

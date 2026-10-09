/* SPDX-License-Identifier: GPL-3.0-only */
/* Bryo: how every modulation target behaves (tests/mod_audit.sh runs it, tests/mod_audit.py reads what it writes).
 *
 * For each knob a slot can move: track 1 alone, set up so the knob has something to act on (its device on, the knob
 * mid-range when its default sits at an end, the knobs it depends on set: STRT needs LEN under 100 and so on), then
 * 3 s without and 3 s with an LFO on it (SIN, 1 Hz; depth 50, or 100 for a switch, which needs the full swing to
 * cross half a step). Both renders are written as mono WAVs, b_G_P.wav and m_G_P.wav (G the target, P the pass:
 * SYNTH and POLY have two, a phrase of notes and one held note, so a knob that only acts at the next note shows as
 * one). targets.csv lists the targets. With "cost", nothing is written: each render's instructions are dumped by
 * callgrind (client requests), base_G_P and mod_G_P.
 *
 * It includes tests/bryo_host.c with its main renamed (mod_audit.sh makes that copy). */
#include "bh_copy.c"
#include <valgrind/callgrind.h>

#define SECS 3.0
static int16_t wbuf[(int)(SECS * 44100) + 64];
static void wav_mono(const char *path, uint32_t n)
{
    FILE *f = fopen(path, "wb");
    uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
                     0x44, 0xAC, 0, 0, 0x88, 0x58, 0x01, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a'};
    uint32_t b = n * 2u;
    h[40] = b & 255; h[41] = (b >> 8) & 255; h[42] = (b >> 16) & 255; h[43] = b >> 24;
    b += 36; h[4] = b & 255; h[5] = (b >> 8) & 255; h[6] = (b >> 16) & 255; h[7] = b >> 24;
    fwrite(h, 1, 44, f);
    fwrite(wbuf, 2, n, f);
    fclose(f);
}

static int phrase, deep;                                   /* SYNTH/POLY: 1 a note every 0.25 s, 0 one held note */
static void run(uint32_t blocks, int rec, int cost)
{
    static const uint8_t NOTES[8] = {0, 7, 12, 10, 3, 7, 5, 2};
    uint32_t b, i, n = 0;
    if (cost)
        CALLGRIND_ZERO_STATS;
    for (b = 0; b < blocks; b++) {
        if (tp[0].src != SRC_TAPE)
            fm1_in.notes = phrase ? note_bit_of_white(NOTES[(b / 345u) % 8u]) : note_bit_of_white(0);   /* (a note
                                                                                                           * every 0.25 s) */
        chain_block(out, CTL);
        if (!cost)
            chain_poll();
        if (rec)
            for (i = 0; i < CTL; i++)
                wbuf[n++] = (int16_t)clamp((out[2u * i] + out[2u * i + 1u]) / 2, -32768, 32767);
    }
}

/* the setup for target g: track 1 alone, its device audible, the knob mid-range if its default is at an end */
static void setup(uint32_t g)
{
    uint32_t k, a = mod_tarr(g, &k);
    const pdesc_t *d = mod_tdesc(g);
    int16_t *v = mod_base(0, a);
    power_on();
    track[1].mute = track[2].mute = track[3].mute = 1;
    sys.playing = 1;
    sys.bpm = 120;
    sys.keys_live = 1;
    sys.sel = 0;
    tp[0].dev[DEV_SRC][TK_REEL] = 2;                 /* KEYS: tonal and rhythmic */
    tp[0].src = a == MA_SYN ? SRC_SYNTH : a == MA_POL ? SRC_POLY : SRC_TAPE;
    if (a == DEV_GRAIN)
        tp[0].dev[DEV_GRAIN][GP_WET] = 60;
    if (a == DEV_RESO)
        tp[0].dev[DEV_RESO][RP_WET] = 50;
    if (a == DEV_COLOR) {
        tp[0].dev[DEV_COLOR][CP_DRIV] = 30;
        tp[0].dev[DEV_COLOR][CP_CRSH] = 20;
        tp[0].dev[DEV_COLOR][CP_NOIS] = 20;
    }
    if (a == DEV_SPACE) {
        tp[0].dev[DEV_SPACE][SP_DLY] = 40;
        tp[0].dev[DEV_SPACE][SP_VERB] = 40;
    }
    if (d->fmt != F_ENUM && (v[k] == d->min || v[k] == d->max))
        v[k] = (int16_t)((d->min + d->max) / 2);
    if (g == MOD_TSRC + TK_SPD)                      /* (SPD's middle is a stopped tape) */
        v[k] = 100;
    {                                                /* where the knob has something to act on */
        if (g == MOD_TSRC + TK_STRT)
            tp[0].dev[DEV_SRC][TK_LEN] = 40;
        if (g == MOD_TG(DEV_GRAIN, GP_OFST))
            tp[0].dev[DEV_GRAIN][GP_SCAN] = SCAN_POS;
        if (g == MOD_TG(DEV_GRAIN, GP_SCAL))
            tp[0].dev[DEV_GRAIN][GP_PRND] = 7;
        if (g == MOD_TSRC + SRC_POLY * NPK + PL_DEC)
            tp[0].pol[PL_SUS] = 30;
        if (g == MOD_TG(DEV_RESO, RP_RES))
            tp[0].dev[DEV_RESO][RP_CUT] = 60;
    }
}

static void modulate(uint32_t g)
{
    param_engine(0, 0, ME_WAVE);
    tp[0].mod[0][0] = 72;                            /* ~1 Hz */
    tp[0].mod[0][12] = 0;                            /* FREE */
    mod_nudge(0, 0, g, deep ? 100 : 50);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    int cost = argc > 2 && !strcmp(argv[2], "cost");
    uint32_t only[16], nonly = 0, g, nb = (uint32_t)(SECS * 1378.125), warm = 400;
    char p[512];
    FILE *list;
    if (argc > 2 && !strcmp(argv[2], "only")) {       /* only G G G ...: just those targets */
        int i;
        for (i = 3; i < argc && nonly < 16u; i++)
            only[nonly++] = (uint32_t)atoi(argv[i]);
    }
    snprintf(p, sizeof p, "%s/targets.csv", dir);
    list = cost ? 0 : fopen(p, "w");
    if (list)
        fprintf(list, "g,arr,k,label,min,max,fmt,deep\n");
    for (g = 0; g < MOD_NTGT; g++) {
        const pdesc_t *d = mod_tdesc(g);
        uint32_t k, a = mod_tarr(g, &k), pass;
        if (!d)
            continue;
        if (nonly) {
            uint32_t j, hit = 0;
            for (j = 0; j < nonly; j++)
                hit |= only[j] == g;
            if (!hit)
                continue;
        }
        deep = d->fmt == F_ENUM || d->max - d->min <= 4;   /* (a switch needs the full swing to cross half a step) */
        if (list)
            fprintf(list, "%u,%u,%u,%s,%d,%d,%u,%d\n", g, a, k, d->label, d->min, d->max, d->fmt, deep);
        for (pass = 0; pass < (a == MA_SYN || a == MA_POL ? 2u : 1u); pass++) {
            phrase = pass == 0;
            setup(g);
            run(warm, 0, 0);
            if (cost) {
                run(500, 0, 1);
                snprintf(p, sizeof p, "base_%u_%u", g, pass);
                CALLGRIND_DUMP_STATS_AT(p);
            } else {
                run(nb, 1, 0);
                snprintf(p, sizeof p, "%s/b_%u_%u.wav", dir, g, pass);
                wav_mono(p, nb * CTL);
            }
            setup(g);
            modulate(g);
            run(warm, 0, 0);
            if (cost) {
                run(500, 0, 1);
                snprintf(p, sizeof p, "mod_%u_%u", g, pass);
                CALLGRIND_DUMP_STATS_AT(p);
            } else {
                run(nb, 1, 0);
                snprintf(p, sizeof p, "%s/m_%u_%u.wav", dir, g, pass);
                wav_mono(p, nb * CTL);
            }
        }
    }
    if (list)
        fclose(list);
    return 0;
}

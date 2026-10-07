# Moss UI assessment: every screen, and how many share one pattern

Written 2026-10-07 against the Felucca 1.0.3 import (`b22a24b`). I haven't changed any firmware yet. This is
the map I want before touching the drawing code, plus the tests that make a refactor safe.

## TL;DR

- Upstream's host renderer draws **116 screens**. I can run it without the JieLi toolchain now
  (`tests/ui_host.sh`). They come out clean: 0 layout lint findings and 0 alignment misses (out of 2.2M
  measurements) in all 10 palettes and both styles.
- **91 of the 116 screens (78%) use the same frame:** a 24 px header, four 57x44 cards, a 122 px panel and
  a 38 px footer. Only 25 screens have their own layout: menu and about (10), confirm dialogs (7), NAME (6),
  and update mode / calibration (2).
- The frame is shared, but **what goes in it isn't described anywhere in one place.** 48 of those 91
  screens get their cards from 15 hand-written branches. The screen type (`GR_*`) is checked 117 times
  across 5 files, so a new or changed screen means editing about 14 functions.
- My proposal: a **page-kind table**. Each screen type gets one row (cards, panel, footer, actions, knobs,
  LARGE mode, redraw signature), and the shared frame code reads the table. I'd migrate one column at a
  time, and each step has to pass **`tests/ui_golden.py`: all 1,392 renders pixel-identical.**

## How I looked at them

```sh
pip3 install Pillow fonttools
tests/ui_host.sh GREY MONO
```

`tests/ui_host.sh` is new. Upstream's `tests/run_tests.sh` stops if `build/felucca.fwsc` is missing, and
building it needs the JieLi clang and the AC79 SDK. The UI renderer itself only needs the generated headers,
and those come from plain Python (`tools/gen_*.py`). So the script runs the same generators
`tools/build.py` runs, then:

1. `tests/ui_render.c` (upstream): every screen in every palette, from the real drawing code, plus the
   layout lint, alignment and draw cost.
2. `tests/ui_render.py` (upstream): PNGs and contact sheets in `build/ui_new/`.
3. `tests/ui_golden.py` (new): a SHA-256 of every raw render in `tests/ui_golden.txt` (1,392 = 116 screens
   x 12 palette/style sets). The renders come out identical run to run and at `-O1` and `-O2`, so any
   difference is a real pixel change.
4. `tests/ui_screens.py` (new): checks that every rendered screen has a row in `tests/ui_screens.tsv` (the
   pattern map below) and writes one contact sheet per pattern to `build/ui_new/patterns/`. A new screen
   can't land without being classified.

It takes about 30 s. I also ran upstream's `ui_test`, `theme_test` and `text_ref_test` against the
generated headers, and all three pass.

## The frame most screens share

```
y   0 ┌──────────────────────────────┐ header      24 px  play state, BPM, track, battery, messages
   24 ├───────┬───────┬───────┬──────┤ 4 cards     44 px  K1..K4: icon, label, value, unit, gauge
   76 ├───────┴───────┴───────┴──────┤ panel      122 px  scope / chart / list / editor / note / strips
  202 ├──────────────────────────────┤ footer      38 px  row 1: steps or key hints; row 2: engine, sound, page
  240 └──────────────────────────────┘
```

MENU > LARGE uses the same frame with 104 px cards and a 62 px strip for the panel.

Every screen gets one value in each of four columns (`tests/ui_screens.tsv`):

| Column | Values (count of 116) |
| --- | --- |
| shell | PAGE 76, LAYER 15, MENU 10, DIALOG 7, NAME 6, INFO 2 |
| cards | TABLE 31 + HOME 12 (data-driven), CUSTOM 48 (15 branches), none 25 |
| panel | SCOPE 25, KEYMAP 15, EDITOR 18 (roll 12, grid 4, slices 2), CHART 15 (9 kinds), LIST 10, NOTE 5, STRIPS 3 |
| footer | STEPS 58, KEYS 15, ACTIONS 14, GRID 4 |

LAYER deserves its own mention: the quick layers (FX, GLO, SCL, EDIT) draw the **same frame** at the same
coordinates, but `ui_layer.c` has its own copy of the cards, panel blit, footer and redraw signature. To a
user it's the same page shape, and in the code it's a second implementation.

## Where the per-screen code lives

This is what makes a change expensive. The screen type is a `graph` enum on each `PAGES[]` row
(`params.c:331`), and every part of the frame asks it separately:

| What | Where | How it decides |
| --- | --- | --- |
| cards | `ui_draw.c` `draw_columns` (lines 635–846) | 12 `if (graph == …)` branches, then the generic loop |
| layer cards | `ui_layer.c` `layer_cards` | 3 more branches |
| panel | `ui_graph.c` `draw_graph` | a 17-case `switch`, and each case sets `cv_oy` itself |
| panel redraw | `ui_graph.c` `graph_signature` | about 15 hand-written `if`s, one per state a panel shows |
| footer | `ui_draw.c` `draw_foot` | `act_cols()` / `grid_on()` predicates |
| actions | `ui.c` `act_cols`, `act_col`, `act_name`, `act_ready`; `ui_input.c` `act_do` | 5 functions with the same `if` ladder each |
| knobs | `ui_input.c` (lines 407–491, 1004) | another ladder |
| LARGE mode | `ui.c` `large_kind`, `ui_graph.c` `strip_kind` | lists of `GR_*` |
| visibility | `ui.c` `page_visible` | per type |

The counts: `GR_SONG` appears 14 times in 4 files, `GR_USER` 14, `GR_PATS` 13, `GR_SLICES` 13. Adding a
page like SONG means finding and extending every one of those ladders. Miss one and you get a page whose
OCT+ hint says LOAD while OCT+ does nothing, or a panel that doesn't redraw because its signature misses
a field.

### What's already shared (and works)

Upstream did a lot right, and I want to keep it:

- `draw_column` / `draw_act_column`: one card renderer for every card, with ellipsis, LARGE, motion icons
  and gauges built in.
- `list_row` + `panel_note`: PRESETS, USER, PROJECT, PHRASES and SONG already draw their rows and empty
  states through the same two functions.
- `cv_key_row`: the keycap hint row. The ACTIONS footer and the layers' KEYS footer both use it already;
  only the code deciding which one to show is separate.
- `page_over`: the layers already swap in their own `page_t`. That's half of the unification done.

### The repetition inside the card branches

The 15 custom branches make 51 `draw_column`/`draw_act_column` calls, and they repeat the same
four things over and over:

- **An empty card**, written out as `draw_column(c, "", "", "", T_THEME, -1, ICON_NONE)` 7 times.
- **"n / total"**: `fmt_int` + `str_cpy(u, "/")` + `fmt_int`, built by hand for STEP, No., SLICE and SLOT.
- **Dimmed when unused**: `used ? VAL(c) : T_DIM` in 9 places.
- **An action card**: LOAD, SAVE, CLEAR, SPLIT and JOIN, each colour-gated by its own readiness test.

## The proposal: a page-kind table

One row per screen type, in one file:

```c
typedef struct {
    char label[8], val[12], unit[8];
    uint16_t color;            /* VAL(c), T_DIM, T_ACCENT */
    int16_t ratio;             /* gauge 0..1000, -1 none */
    uint16_t icon;
    uint8_t kind;              /* CARD_VALUE, CARD_ACTION, CARD_EMPTY */
} card_t;

typedef struct {
    void (*cards)(card_t out[4]);                 /* NULL: PAGES[] ids through the generic loop */
    void (*panel)(const track_t *t, uint16_t c);  /* NULL: the scope */
    uint32_t (*panel_sig)(const track_t *t);      /* what the panel shows, for the lazy redraw */
    uint8_t panel_oy;                             /* GOY for a 100 px chart, 0 for full height */
    uint8_t foot;                                 /* FT_STEPS, FT_ACTIONS, FT_GRID, FT_KEYS */
    uint8_t large;                                /* LK_LABEL, LK_TALL */
    const page_act_t *act;                        /* the action cards: mask, names, ready(), run() */
    int (*knob)(uint32_t k, int32_t d);           /* NULL: edit the param in that column */
} page_kind_t;

static const page_kind_t KINDS[GR_COUNT] = { … };
```

How it works: `draw_columns`, `draw_graph`, `draw_foot` and the action and knob code each become one lookup
(`KINDS[cur_page()->graph]`) followed by the shared code. A screen's whole behaviour is in its row and the
functions it points to. Why this shape: it keeps upstream's `PAGES[]` table and its draw primitives as they
are. It only replaces the scattered `if` ladders with data, so the risk is in plumbing, not pixels.

Small helpers take care of the repetition: `card_empty()`, `card_count(label, i, n)` for "3 / 16",
`card_act(label, ready)`, and `card_dim(card, used)`.

The layers become four more rows (their cards, the KEYMAP panel, the KEYS footer), which removes the second
copy of the frame from `ui_layer.c`.

### Expected payoff

- A new screen means one `KINDS` row plus its card and panel functions in one file, instead of about 14
  edits in 4–5 files.
- The 117 `GR_*` checks drop to roughly the table plus a handful of genuine special cases (STEP's EDIT
  clear, the SONG row cursor).
- `panel_sig` sits next to the panel it describes, so a forgotten field stops being an invisible
  stale-screen bug.
- The playhead fix from my UI redraw plan gets a natural home: `FT_STEPS` can own a separate small playhead
  update instead of every step changing `foot_sig` and redrawing the whole 240x38 footer.

### Migration order (each step is its own commit)

Every step has to pass `tests/ui_host.sh` with **0 renders changed** (`ui_golden.py`) plus upstream's
`ui_test`. Pixel-identical is the bar for this refactor. A visual change is a separate commit that updates
the fingerprints on purpose.

1. **`card_t` and the helpers.** Rewrite the 15 card branches to fill `card_t[4]` and draw them in one loop.
   This is mechanical, and the biggest readability win. **Done** (see "Step 1, as built" below).
2. **`KINDS` for the panel.** Replace the `draw_graph` switch and the per-case `cv_oy`.
3. **Actions.** Fold `act_cols` / `act_col` / `act_name` / `act_ready` / `act_do` into `page_act_t` rows.
4. **Footer kinds**, so the layers' key row and the ACTIONS footer go through one path.
5. **Layers as kinds**, so `ui_layer.c` keeps only its key maps.
6. **Signatures per kind** (`graph_signature` split up).
7. **Knobs per kind** (`ui_input.c`).

Steps 1–2 are where I'd start; they touch only drawing code, and the golden renders cover it fully. Steps 3
and 7 touch behaviour, so they also lean on `ui_test.c` (which already covers the loaders, undo, REC and
the drum grid).

### Step 1, as built

How it works: every page now fills four `card_t` (label, value, unit, colour, gauge, icon) through a small
filler function, `cards_<kind>()`, and `cards_draw()` draws them with upstream's `draw_column`, which I left
unchanged. The fillers are in `firmware/src/ui_draw.c` (`cards_home`, `cards_table`, `cards_step`,
`cards_song` and so on) and `firmware/src/ui_layer.c` (`cards_layer_fx`, `cards_layer_glo`). Four helpers
cover the repetition: `card_empty`, `card_count` ("3" over "/16"), `card_act` (an OCT+ action) and
`card_flag` ("ON" / "--").

The catch: `draw_column` reads two globals the moment it's called. `fmt_named` (set by `param_format`) says
the value is a name and shouldn't roll its digits, and `card_mot_next` (set by `card_mot_of`) puts MOTION's
icon on the card. Filling all four cards first and drawing afterwards would let the later cards overwrite
those flags. So `card_set` copies both into the card and clears them, exactly as `draw_column` did, and
`cards_draw` restores each one just before it draws that card.

How I verified it:

- `tests/ui_golden.py`: all 1,392 renders pixel-identical.
- The same comparison against the unmodified code for the outputs the fingerprints don't cover: the
  `FELUCCA_FM4=1` build's screens and the rolling-digit filmstrips. 2,916 files, all identical.
- Upstream's `ui_test.c` passes in both builds, including its rolling-digit check, which exercises the
  `fmt_named` path.
- `tests/ui_screens.py` now also checks that every `CUSTOM:<x>` in the screen map has a `cards_<x>()`
  function, so the map stays tied to the code.

Still open: the host timing of an idle HOME frame (nothing changed on screen) reads about 5 µs slower
(roughly 25 → 30 µs) across four alternating runs. Copying four small cards per frame shouldn't cost that
much, and wall-clock timing in this container is noisy, so I'm checking it with an instruction count
(valgrind) before calling it real. The texts drawn, glyph pixels read and pixels blitted are identical
before and after.

## Design notes from the walk-through

These aren't refactor items; they're what I noticed looking at all 116 screens together.

- **12 pages show the scope as filler**, plus EDIT on every engine without a chart (ANALOG, GRAIN, PHYS and
  others): ENV DEST, LFO DEST, ARP, ARP 2, DLY, REVERB, CHORUS, VOICE, VOICE 2, GLOBAL, SYSTEM and MOTION.
  Their panel is the scope only because `draw_graph` falls back to it. For ARP or DLY, a panel that shows the page's effect (the arpeggio's
  notes, the delay taps) would teach more than a waveform. With the table in place, each would be one new
  panel function.
- **Moving filler and reduced motion.** The scope animates on those pages (`graph_signature` mixes in
  `ui.frame` for them), and ANIM OFF (`PREF_ANIM_OFF`) stops the rolling digits and the roll's glide but not
  the scope. On a page where the scope is decoration, a still panel
  under ANIM OFF would be the accessible default. I'd treat this as a design decision for us, not a bug.
- **Empty states are already consistent** (`panel_note`). CHORD OFF, NO FAVORITES, an empty SONG and TOOLS
  read as one family, and I'd keep it that way.
- **Ellipsis is limited to free text.** The renderer reports 278 ellipsised strings, all preset or pattern
  names in places marked free text. That's working as designed, but in LARGE the MIXER names cut down to
  four letters ("CLOU…"), which is where I'd look first if LARGE is meant for low vision.

## Files

- `tests/ui_host.sh`: the host-only UI run (generators, render, PNGs, golden, patterns).
- `tests/ui_screens.tsv`: the 116 screens and their patterns.
- `tests/ui_screens.py`: the coverage check and the contact sheets per pattern.
- `tests/ui_golden.py` and `tests/ui_golden.txt`: pixel fingerprints of every render.

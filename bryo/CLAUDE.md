# Bryo: context for working on this code

Read [docs/bryo-status.md](docs/bryo-status.md) first: what Bryo is, where it stands, the file map, how to build,
test and measure, the decisions and the open questions. The per-part detail ("as built" sections) is in
[docs/bryo-architecture.md](docs/bryo-architecture.md); every control is in [docs/controls.tsv](docs/controls.tsv).

## Commands

```sh
tests/run_tests.sh                       # must end "ALL HOST TESTS PASSED"
sh tests/checkpoint_sim.sh 10            # load per run (callgrind) + interrupt stress ("the books balance")
sh tests/mod_audit.sh [--cost]           # every modulation target: change, steps at block edges, cost
clang -fsyntax-only -m32 -ffreestanding -fno-builtin -Wall -Wno-unused-function \
      -Ifirmware/hal -Ifirmware/src -Ibuild/gen firmware/src/bryo.c          # target type check: no warnings
python3 tests/ui_golden.py update tests/bryo_golden.txt build/bryo_ui        # after a deliberate screen change,
sed -i '2s|.*|# Bryo: SHA-256 of every Bryo screen render (tests/bryo_host.c, tests/ui_golden.py)|' tests/bryo_golden.txt
```

The host binary alone: `cc -O1 -I build/gen -Itests -o build/bryo_host tests/bryo_host.c -lm && ./build/bryo_host
build/bryo_ui`. The device build (`./build.sh`) needs JieLi's toolchain, which isn't reachable from a cloud
container; everything is verified on the host.

## Rules

- Every new file starts with `/* SPDX-License-Identifier: GPL-3.0-only */` (`#` form for scripts).
- Never touch the update loader's path (`firmware/src/ota_hw.c`, `firmware/loader/`). The HAL (`firmware/hal/`) is
  header-only: add to it, don't change it.
- Firmware code: integers only, no 64-bit arithmetic, no compiler builtins (the target compiler makes them library
  calls). Audio is Q15; gains Q12/Q10; fractions Q8.
- The audio ISR (`chain_block`, a 32-sample block) never allocates or waits. The main loop owns memory: shorten
  the list or set the flag, `RING_PUBLISH()`, then free. Lists the ISR reads whole are double-buffered (build the
  half it isn't reading, flip one byte).
- Measure before optimizing, with the tools above, and check that a measure means what it seems to (the
  modulation audit's history in bryo-status.md is why).
- Docs are first person, casual, technical where needed, and say how and why. Each part keeps an "as built"
  section in the architecture doc; keep bryo-status.md current when the state changes.
- Commits are titled "Bryo: ..." and describe what changed and why. No PR unless asked; never push upstream.

## Where things have to change together

- A new `.c` module: `firmware/src/bryo.c` and `tests/bryo_host.c` include every module in the same order; add it
  to both, and to `tests/coverage.py`.
- A new knob: its descriptor in `param.c` (up to 20 a device: `NPK`), its pictogram in `ui_px.c` (`DEV_PK`,
  `SYN_PK`, `POL_PK`, `ME_PK`), the controls map if a gesture changes, the screens' goldens. It's a modulation
  target automatically (`mod.c` numbers targets by `NPK`); a reel choice or anything the main loop allocates for
  must be excluded in `mod_tdesc`.
- Modulation depths are read and set through `mod_dep()` / `mod_dep_set()` (a sorted list per track).
- The device code reads knobs through `TPD(t, array)` (the modulated copy when there are depths), not `tp[t]`;
  the main loop's memory decisions read `tp[t]` and `mod_peak`. Pitches and cutoffs read `mod_pitch16` /
  `mod_fine8`.
- Load changes: re-run `checkpoint_sim.sh` and update the tables in bryo-status.md, bryo-architecture.md and
  hardware-checkpoint.md.

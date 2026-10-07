# Bryo: modification notice

Bryo is a modified version of Felucca, distributed under the GNU General Public License,
version 3 only (`GPL-3.0-only`). This file is the "prominent notice" GPL-3.0 section 5(a) asks for,
so here's what that means in practice.

- **Original work:** Felucca, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments.
  Source: <https://github.com/hugelton/Felucca>
- **Based on:** Felucca 1.0.3, commit `b22a24b` (tag `v1.0.3`), imported unmodified on 2026-10-06.
- **Modified by:** the Bryo maintainers ([thoughtgrain](https://github.com/thoughtgrain)), starting 2026-10-06. Every change after the import commit is in
  this repository's git history, with its date.
- **Licence:** unchanged. Bryo stays `GPL-3.0-only` as a whole (full text in [LICENSE](LICENSE)).
  The ported and bundled parts keep their own licences, as listed in [LICENSING.md](LICENSING.md)
  and [LICENSES/](LICENSES/).

## Why I keep the upstream files as they are

`LICENSE`, `LICENSING.md`, `LICENSES/` and every SPDX header stay exactly as upstream wrote them.
They're the record of who owns what, and editing them would blur that. Bryo's own notes live in
this file and in [ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md). New files I add start with the same
`SPDX-License-Identifier: GPL-3.0-only` line the rest of the tree uses.

## What still says "Felucca", and why

Bryo shows its own name wherever a person sees it: the boot screen, ABOUT, the crash screen, the USB
device name (its MIDI and audio ports show as "Bryo"), the editor's version string ("BRYO v0.1.0"),
the serial console, the web editor and installer, and the release file names (`bryo-X.Y.fwsc`).
A few places keep the upstream name on purpose:

- **Credits and copyright.** ABOUT says "Based on Felucca 1.0.3" with Leo Kuroshita's copyright, and
  the credits list opens with Felucca. That's attribution, and it stays.
- **The update loader.** `firmware/loader/` and the strings it compiles from `usb.c` and `ota.c` are
  untouched, so the loader's binary doesn't change. It still enumerates as "Felucca Update", and
  packages still carry its `FELUCCA-LOADER-1` marker, which the installers look for.
- **Compatibility identifiers.** The file formats (`felucca-patch`, `felucca-library`,
  `felucca-backup`), the editor's browser storage names and the SysEx protocol keep their names, so
  files and backups move between Felucca and Bryo. The tools accept a device named Bryo or Felucca.
- **Build internals.** The `FELUCCA_*` build flags, `felucca.c`, `felucca_dbg` and the generated
  `felucca_*.h` headers keep upstream's names. Renaming hundreds of internal identifiers would buy
  nothing on the device, and it would make every upstream fix painful to merge.
- **Upstream's own documents.** `LICENSE`, `LICENSING.md`, `CONTRIBUTING.md`, `BUILDING.md`
  (apart from the release file names) and the README text under its banner describe Felucca, and
  I leave them as upstream wrote them.

## If you distribute Bryo

The same rule Felucca sets applies: if you share Bryo, or firmware built from it, give your
recipients the complete corresponding source under GPL-3.0-only, including when it ships inside a
device. Keep this notice and the upstream credits with it.

## Not affiliated

Bryo is not Felucca and isn't endorsed by Hügelton Instruments. "Felucca" and "Hügelton
Instruments" are names of Hügelton Instruments. "M-VAVE" and "FM-1" are trademarks of their
respective owners; Bryo is independent firmware that runs on FM-1 hardware and isn't affiliated
with, endorsed by or supported by them.

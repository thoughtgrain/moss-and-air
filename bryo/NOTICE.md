# Moss: modification notice

Moss is a modified version of Felucca, distributed under the GNU General Public License,
version 3 only (`GPL-3.0-only`). This file is the "prominent notice" GPL-3.0 section 5(a) asks for,
so here's what that means in practice.

- **Original work:** Felucca, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments.
  Source: <https://github.com/hugelton/Felucca>
- **Based on:** Felucca 1.0.3, commit `b22a24b` (tag `v1.0.3`), imported unmodified on 2026-10-06.
- **Modified by:** the Moss maintainers ([thoughtgrain](https://github.com/thoughtgrain)), starting 2026-10-06. Every change after the import commit is in
  this repository's git history, with its date.
- **Licence:** unchanged. Moss stays `GPL-3.0-only` as a whole (full text in [LICENSE](LICENSE)).
  The ported and bundled parts keep their own licences, as listed in [LICENSING.md](LICENSING.md)
  and [LICENSES/](LICENSES/).

## Why I keep the upstream files as they are

`LICENSE`, `LICENSING.md`, `LICENSES/` and every SPDX header stay exactly as upstream wrote them.
They're the record of who owns what, and editing them would blur that. Moss's own notes live in
this file and in [ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md). New files I add start with the same
`SPDX-License-Identifier: GPL-3.0-only` line the rest of the tree uses.

## If you distribute Moss

The same rule Felucca sets applies: if you share Moss, or firmware built from it, give your
recipients the complete corresponding source under GPL-3.0-only, including when it ships inside a
device. Keep this notice and the upstream credits with it.

## Not affiliated

Moss is not Felucca and isn't endorsed by Hügelton Instruments. "Felucca" and "Hügelton
Instruments" are names of Hügelton Instruments. "M-VAVE" and "FM-1" are trademarks of their
respective owners; Moss is independent firmware that runs on FM-1 hardware and isn't affiliated
with, endorsed by or supported by them.

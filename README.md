# moss-and-air

## bryo/

[Bryo](bryo/) is my firmware for the M-VAVE FM-1, a 4-track sound-sculpting instrument and a fork of [Felucca](https://github.com/hugelton/Felucca),
the GPL-3.0-only replacement firmware by Leo Kuroshita / Hügelton Instruments. It starts from Felucca 1.0.3
(`b22a24b`), imported unmodified so every change after that is easy to see in the history. (The name: *bryon*
is Greek for moss.)

The thanks, and how I try to be a good downstream, are in
[bryo/ACKNOWLEDGEMENTS.md](bryo/ACKNOWLEDGEMENTS.md). The GPL modification notice, including what still says
"Felucca" and why, is in [bryo/NOTICE.md](bryo/NOTICE.md). Everything inside `bryo/` is GPL-3.0-only, same as
upstream.

Where it stands: phases 1 to 7 of 10 are built and verified on the host (nothing has run on an FM-1 yet); see
[bryo/docs/bryo-status.md](bryo/docs/bryo-status.md) for the state, the file map, how to build and test, the
decisions and what's open, and [bryo/docs/bryo-architecture.md](bryo/docs/bryo-architecture.md) for each part as
built.

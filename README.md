# moss-and-air

## bryo/

[Bryo](bryo/) is my firmware for the M-VAVE FM-1, a fork of [Felucca](https://github.com/hugelton/Felucca),
the GPL-3.0-only replacement firmware by Leo Kuroshita / Hügelton Instruments. It starts from Felucca 1.0.3
(`b22a24b`), imported unmodified so every change after that is easy to see in the history. (The name: *bryon*
is Greek for moss.)

The thanks, and how I try to be a good downstream, are in
[bryo/ACKNOWLEDGEMENTS.md](bryo/ACKNOWLEDGEMENTS.md). The GPL modification notice, including what still says
"Felucca" and why, is in [bryo/NOTICE.md](bryo/NOTICE.md). Everything inside `bryo/` is GPL-3.0-only, same as
upstream.

The screen work so far (every screen rendered and grouped by pattern, the plan for unifying them, and the
pixel-identical gate each step has to pass) is in [bryo/docs/ui-assessment.md](bryo/docs/ui-assessment.md).

# Thank you, Felucca

Moss exists because [Felucca](https://github.com/hugelton/Felucca) exists. Leo Kuroshita
([@kurogedelic](https://github.com/kurogedelic)) and Hügelton Instruments did the hard part: they
reverse-engineered the M-VAVE FM-1, wrote a bare-metal firmware for its JieLi AC79 from scratch,
and shipped thirteen engines, a 4-track sequencer, USB audio and a web installer anyone can use.
Then they released all of it under the GPL so people like me can learn from it and build on it.

I'm grateful for that, and I want it to be obvious to anyone who lands here. If Moss is useful to
you, the credit for the foundation goes upstream.

## What I started from

I forked Felucca **1.0.3** (commit `b22a24b`, tag `v1.0.3`). The first commit in this folder is an
unmodified snapshot of that tree, so `git diff` against it shows exactly what Moss changed and
nothing else.

## How I try to be a good downstream

- **Upstream comes first for bugs that are theirs.** If I find a bug that also exists in Felucca, I
  report it there (one issue per bug, as their [contributing guide](CONTRIBUTING.md) asks) and
  offer the fix back, instead of only patching it here.
- **Ideas go to their Discussions**, not their issue tracker.
- **I never push to upstream directly.** Anything I offer goes through a pull request they're free
  to decline.
- **Their name stays theirs.** "Felucca" and "Hügelton Instruments" are Hügelton's names, which is
  part of why this fork is called Moss. Where the code or docs say "Felucca" they describe the
  upstream project, and I leave those credits in place.

## Support the people who made this

- Sponsor Hügelton: <https://github.com/sponsors/hugelton>
- Felucca on itch.io: <https://hugelton.itch.io/felucca>

## Everyone upstream credits, still credited

Felucca stands on other people's work too, and that carries straight through to Moss. The full list
with licences is in [LICENSING.md](LICENSING.md); in short: DaisySP (Electrosmith, Emilie Gillet),
Rings (Emilie Gillet), msfa (Google, Pascal Gauthier, via Dexed), klattsch (Tony Gies, as a design
reference), the Inter Tight font (The Inter Project Authors), the Fukiai icon font (Hügelton), the
VSCO-2 CE and VCSL sample libraries (Versilian Studios) and three files from the JieLi AC79 SDK.

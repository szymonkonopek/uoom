# ⬇︎ [**DOWNLOAD `DOOM1.WAD`**](https://github.com/szymonkonopek/uoom/raw/main/wad/DOOM1.WAD)

### → [github.com/szymonkonopek/uoom/raw/main/wad/DOOM1.WAD](https://github.com/szymonkonopek/uoom/raw/main/wad/DOOM1.WAD)

**Click that link and the 4 MB file downloads.** Then copy it into the UOOM
app's own directory on the watch, next to the `.uapp`, and start the app again.

> If you got here by scanning the code on your watch: that is the whole job —
> download, copy, done. The watch is showing this link because it found no game
> data to run.

---

The rest of this page is provenance, for anyone who wants to know exactly what
that file is.

## What it is

The shareware DOOM IWAD, committed so that cloning this repository is enough to
run the port. `tools/fetch-wad.sh` is still there for Freedoom, or to re-fetch
this file.

| | |
|---|---|
| Size | 4 196 020 bytes |
| Lumps | 1264 |
| MD5 | `f0cefca49926d00903cf57551d901abe` |
| SHA-256 | `1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771` |
| Version | shareware **v1.9** |
| Contents | Episode 1, *Knee-Deep in the Dead* — E1M1 to E1M9 |

v1.9 rather than v1.8 (`5f4eb849b1af12887dec04a2a12e5e62`, same size and lump
count), because doomgeneric is a 1.9-derived engine and DOOM's demos are
version-locked. Against a v1.8 WAD the engine says so:

```
Demo is from a different game version!
(read 108, should be 109)
*** You may need to upgrade your version of Doom to v1.9. ***
    This appears to be v1.8.
```

and then plays the demo anyway — doomgeneric comments out the `I_Error` that
vanilla raises here — so the attract loop runs against a mismatched input
stream and diverges from what was recorded. That is where the `This appears to
be v1.8.` line in older `uoom.log`s came from. On v1.9 the message does not
appear at all.

## Licence

id Software released the shareware DOOM for free copying and non-commercial
redistribution, which is why it has been on public archives since 1993 and is
packaged by Linux distributions to this day. It is **not** under the GPL — the
GPL covers the DOOM *source*, released in 1997, and never covered the game data.
Bethesda owns it now.

One honest caveat, since it is the difference between "everyone does this" and
"the licence says so": those terms are written around the *complete, unmodified
shareware package* — the DOS distribution, `doom19s.zip`, installer and all. A
bare extracted `DOOM1.WAD` is a component of it rather than that package. Wide
practice, including a great many repositories that ship exactly this file beside
an engine, has treated the two as equivalent for thirty years, and id and
Bethesda have never suggested otherwise. But the two are not the same statement,
and this file is here as a deliberate decision rather than an oversight.

The registered IWADs — `DOOM.WAD`, `DOOM2.WAD` — are a different matter
entirely and are not redistributable. Buy them; the port reads them fine.

## Also here

This directory is the host harness's filesystem root, the way the app's own
directory is on the watch. Running `uoom-host --wad wad` writes `uoom.log`,
`doomsav*.dsg` and screenshots here; `.gitignore` keeps all of it untracked.

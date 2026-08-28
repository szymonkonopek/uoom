# Memory: the number this port lives or dies by

Everything here is **measured**, not estimated. The host harness (`host/`)
builds the same DOOM, the same port layers and the same 240x240 ABGR2222
pipeline that run on the watch, and it reports DOOM's own zone accounting, so
these numbers come out of a real run rather than out of arithmetic.

Reproduce any row with one command; each is given below.

## Read this before trusting any number below

**The host harness overstates the requirement by about a third**, and every
figure on this page is a host figure unless it says otherwise.

DOOM's level structures are pointer-dense, and a pointer is 8 bytes on a laptop
and 4 on the watch. Measured with `tools/struct-sizes.sh`, which cross-compiles
a probe for `armv7m-none-eabi` and reads the sizes out of a deliberate type
error (no ARM toolchain required):

| structure | 32-bit ARM | this host | inflation |
|---|---|---|---|
| `line_t` | **44** | 72 | +64% |
| `seg_t` | **32** | 56 | +75% |
| `side_t` | **20** | 24 | +20% |
| `sector_t` | **88** | 128 | +45% |
| `subsector_t` | **8** | 16 | +100% |
| `mobj_t` | **156** | 224 | +44% |
| `node_t` | **36** | 36 | — |
| `vertex_t` | 8 | 8 | — |
| `visplane_t` | 664 | 664 | — |

So: budget for the watch off the ARM column, use the host to find *which*
allocation is the problem, and confirm the absolute numbers on device.

## The headline

Two things changed this page after it was first written, and both moved in the
port's favour.

**The IWAD matters more than anything else.** Measured across all three
attract-mode demos — E1M1, E1M5, E1M7, the standard shareware stress set:

| IWAD | zone floor (host) | zone peak used (host) |
|---|---|---|
| **shareware `DOOM1.WAD`** | **1024 KB** | **571 KB** |
| Freedoom Phase 1 | 2048 KB | 1680 KB |

Freedoom's maps are roughly four times the geometry of the shareware maps
everybody remembers. Every earlier figure on this page was Freedoom's, and it
was the wrong worst case to design against.

**And the app's RAM region is not where the zone belongs.** Reading the SDK
source settled what `UNA_APP_GUI_RAM_LENGTH` actually is:

- It is a **linker ceiling, not a reservation.** The `.uapp` header records real
  section sizes and the loader allocates from those; nothing in the SDK
  validates or caps the number. **Raising it costs nothing at run time.**
- The app's linker script has **one MEMORY region and no flash region** —
  `.text` and `.rodata` live in RAM alongside `.bss`, so DOOM's code and its
  `const` trig tables count against that ceiling.
- But `malloc` does **not**. `_sbrk` is a hard trap; `malloc` forwards to the
  kernel's allocator, whose heap is **outside the app region entirely**.

So the zone — the one big elastic object — now comes from `malloc`
(`uoom_zone_base()`), which is the opposite of the usual bare-metal advice and
correct here. What is left inside `RAM_LENGTH` is DOOM's code, its static
arrays, the 57.6 KB panel buffer and the 64 KB screen buffer.

| | |
|---|---|
| Zone, shareware `DOOM1.WAD` | **~1 MB, from the kernel heap** |
| Static RAM (`.data` + `.bss`, panel + screen buffers included) | ~297 KB (host build) |
| GUI stack DOOM needs (BSP recursion + drawers) | 24 KB |
| `UNA_APP_GUI_RAM_LENGTH` set to | 900 KB — a ceiling, freely raisable |

**What is still unknown is the size of the kernel's heap**, which the SDK never
states. One message answers it on device: `RequestMemoryInfo` reports
`totalHeap`, `freeHeap` and `largestFreeBlock`. That is question #1 in
`docs/07-open-questions.md` and it is a five-line addition to the platform
adapter.

## How the zone floor was found

`I_ZoneBase` normally asks for 6 MB and refuses to start below 6 MB. Patch 0002
replaces it with `uoom_zone_base()`, which requests `UOOM_ZONE_BYTES` from the
kernel allocator and steps down in 64 KB increments until it is satisfied — so
a smaller-than-hoped zone still plays rather than refusing to start.

### Shareware `DOOM1.WAD`, all three demos

```sh
for kb in 384 512 640 768 1024; do
  make -C host clean && make -C host EXTRA_DEFS="-DUOOM_ZONE_BYTES=$((kb*1024))"
  ./host/out/uoom-host --wad host/wad --frames 4000
done
```

| Zone | Result |
|---|---|
| 384 KB | dies loading a level: `failed on allocation of 59440 bytes` |
| 512 KB | dies mid-level: `failed on allocation of 8816 bytes` |
| 640 KB | dies on a composite texture: `32808 bytes` |
| 768 KB | dies on a cached lump: `35120 bytes` |
| **1024 KB** | **plays all three demos**, 453 KB free at the worst point, peak use 571 KB |

Note the failure sizes walking *downward* as the zone grows: at 384 KB it cannot
load the map, at 512 KB it loads and then starves on small allocations, at
640-768 KB it starves on textures. That is the shape of an elastic allocator
running out of room to purge into.

Level geometry for E1M1, from `-DUOOM_LOG_MAP_ALLOC=1`, against the ARM
structure sizes: `segs` 36 512, `lines` 36 300, `sides` 21 060, `nodes` 13 788,
`sectors` 12 584, `vertexes` 3 736, `subsectors` 3 072 — **~127 KB**, against
Freedoom's 499 KB for the same arrays.

### Freedoom Phase 1 — the pessimistic case

Sweeping the same constant against a 700-frame scripted run:

```sh
for kb in 1920 2048 2176 2560 3072; do
  make -C host clean && make -C host EXTRA_DEFS="-DUOOM_ZONE_BYTES=$((kb*1024))"
  ./host/out/uoom-host --wad host/wad --frames 700 \
    --keys "30:e,32:d,60:e,62:d,90:e,92:d,120:e,122:d,220:q,300:a,340:r,380:f"
done
```

| Zone | Result |
|---|---|
| 320 KB | dies in `R_Init` -- `Z_Malloc: failed on allocation of 168 bytes` |
| 512 KB | dies further into `R_Init` |
| 768 KB | dies in `ST_Init` |
| 1024 KB | **boots**, title screen and menus render |
| 1920 KB | menus fine, dies loading the level: `failed on allocation of 246944 bytes` |
| **2048 KB** | **plays**, 302 KB free at the worst point |
| 3072 KB | plays, with ~1.2 MB free -- the extra goes to cache, not to need |

Note the shape of that curve. DOOM's zone is **elastic**: lump caches are
`PU_CACHE` and get purged under pressure, so a smaller zone does not fail
gradually, it fails at the first large *non-purgeable* allocation. Between
2048 KB and 2176 KB the difference is not 128 KB of appetite -- it is whether a
single 247 KB contiguous block can be found.

Peak use is ~1746 KB in every configuration above the floor, which says the
extra memory is going to cache, not to need.

Moving `I_VideoBuffer` out of the zone and into `.bss` (patch 0007) was worth
128 KB off the floor on its own -- not because it saves memory (it is the same
64 000 bytes either way) but because a `PU_STATIC` block that size, allocated
at startup, fragments the arena in front of the 247 KB level allocation that
has to land later.

## Where it goes

Build with `-DUOOM_LOG_MAP_ALLOC=1` and every level-geometry array reports
itself. Freedoom Phase 1 E1M1, with the ARM column computed from the structure
sizes above:

| array | count | host | **ARM** |
|---|---|---|---|
| `segs` | 4409 | 246 904 | **141 088** |
| `lines` | 2818 | 202 896 | **123 992** |
| `sides` | 3972 | 95 328 | **79 440** |
| `nodes` | 1397 | 50 292 | **50 292** |
| `sectors` | 395 | 50 560 | **34 760** |
| `vertexes` | 2760 | 22 080 | 22 080 |
| `subsectors` | 1398 | 22 368 | **11 184** |
| `linebuffer` | — | 31 688 | **15 844** |
| `blockmaplump` | — | 14 508 | 14 508 |
| `blocklinks` | — | 11 520 | **5 760** |
| **total** | | **768 144** | **498 948** |

This is the shape of the problem: **two arrays are 51% of it**, and neither is
compressible without changing a structure. It is also why the *floor* sits at
2048 KB while *peak use* is 1702 KB — the floor is set by whether a single
247 KB contiguous block can be found at level load, not by total demand. At
1920 KB the run dies on exactly that allocation with 200 KB still free.

Tracing every zone allocation over 60 KB during level load:

| Bytes | Tag | When |
|---|---|---|
| 247 984 | `PU_LEVEL` | level load |
| 246 904 | `PU_LEVEL` | level load |
| 119 160 | `PU_STATIC` | level load |
| 115 192 | `PU_LEVEL` | first frames |
| 103 400 | `PU_LEVEL` | first frames |
| 95 328 | `PU_LEVEL` | level load |
| 72 644 | `PU_LEVEL` | level load |
| 68 168 | — | startup |
| 64 000 | `PU_STATIC` | startup (`I_VideoBuffer`) |

Two ~247 KB `PU_LEVEL` blocks dominate. **This is map geometry, and it is a
property of the WAD, not of the engine.** Freedoom Phase 1's maps are
substantially larger than the shareware `DOOM1.WAD` maps everybody remembers,
which is the single biggest caveat on every number on this page --
see "Ways out".

### The intermission screen, and why the level after it was the one that died

`DOOM1.WAD`'s worst map is E1M2, whose geometry comes to 180 410 bytes on ARM,
with a 46 816-byte `segs` array as its largest single allocation:

| map | ARM geometry | largest block (`segs`) |
|---|---|---|
| E1M1 | 86 718 | 23 424 |
| **E1M2** | **180 410** | **46 816** |
| E1M3 | 174 107 | 46 240 |
| E1M7 | 165 583 | 43 872 |
| E1M5 | 139 873 | 36 512 |

That fits a 640 KB zone with room to spare, and yet finishing E1M1 reliably
died on exactly that 46 840-byte allocation (46 816 plus the block header).

The cause is an ordering bug that only a small zone can see. `G_Ticker`
processes `gameaction` at the top of the function -- which is
`G_DoWorldDone` -> `G_DoLoadLevel` -> `P_SetupLevel`, the next level's entire
geometry -- and only *afterwards* notices that the intermission screen has gone
and calls `WI_End` to release its graphics. So the next map is allocated while
the intermission still holds about 150 KB of `PU_STATIC` lumps, in dozens of
small blocks scattered through the arena.

Measured on the host at the moment of failure:

```
zone: wanted 74416, largest run 52952, of 655360
zone:   static  386320 B in  678 blocks, biggest  68208
zone:   free    141872 B in   10 blocks, biggest  29128
```

141 KB free and not one usable hole. Vanilla never noticed, because at 6 MB the
gap was there anyway. Patch 0020 calls `WI_End` at the top of `G_DoWorldDone`
instead, before the level loads, which makes those lumps purgeable in time for
`Z_Malloc` to walk over them; `WI_End` becomes idempotent because the original
late call still happens, and releasing a lump twice is an `I_Error` once the
first release let it be purged.

The general lesson is worth keeping: **on this zone, "free" is not the number
that matters.** Patch 0019 therefore prints the whole layout whenever an
allocation fails, and the panic line now carries the largest run next to the
size that was wanted. A wanted-46840 / largest-13296 failure and a
wanted-46840 / largest-45000 failure look identical from the watch otherwise,
and they need opposite fixes.

## Static footprint, and what was cut

Upstream doomgeneric carries roughly 280 KB of `.data` + `.bss` at
320x200. Patch 0003 cuts the six constants that account for most of it:

| Constant | Upstream | UOOM | Saved | Why it is safe (or not) |
|---|---|---|---|---|
| `MAXVISPLANES` | 128 | **96** | 21.2 KB | `visplane_t` is 664 bytes. **See the story below** -- this number cost a bug fix to establish. |
| `MAXOPENINGS` | `SCREENWIDTH*64` | `/4` | 30.7 KB | Published high-water marks over the full shareware demos are 1678-2106 out of 20 480, so upstream is ~10x over-provisioned. `/4` still leaves 2.4x headroom. Cheapest saving here. |
| `BACKUPTICS` / `NET_MAXPLAYERS` | 128 / 8 | 8 / 1 | 20.3 KB | `ticdata` is network tic backlog for a game that cannot be networked. Free. |
| `MAXDRAWSEGS` | 256 | 128 | 7.7 KB | Demo marks are 65-67. Overflow degrades to hall-of-mirrors, not a crash. GBADoom ships 192. |
| `MAXWIDTH` / `MAXHEIGHT` | 1120 / 832 | `SCREENWIDTH` / `SCREENHEIGHT` | 5.7 KB | Sized for hi-res builds this port will never do. Free. |
| `MAXVISSPRITES` | 128 | 96 | 1.9 KB | Demo marks are 54-67 -- 64 would clip in a busy room. Overflow drops sprites rather than crashing. |
| `MAXSEGS` | 32 | `SCREENWIDTH/2+1` | **-1.3 KB** | Goes the *other* way on purpose: vanilla's 32 is below the theoretical maximum for a 320-wide screen, which Chocolate Doom fixed after Lee Killough showed the bound is a function of screen size. 1.3 KB for correct clipping. |

### RAM diet, stage 1 — done, and provably inert

Patch 0009 takes 24 bytes off `line_t` (88 → 72 host, 68 → **44** ARM):

- **`bbox[4]` removed.** Four `fixed_t` holding nothing but the min and max of
  `v1` and `v2`. Written once at load; read in **exactly one place**
  (`PIT_CheckLine`'s early rejection, verified by grepping every `.c` and `.h`
  — `r_bsp.c`'s `bbox` is `node_t`'s, a different struct). Now computed at that
  one site, which is per-line inside a blockmap cell, not an inner loop.
  **16 bytes per linedef, on both architectures.**
- **`slopetype` narrowed** from an int-sized enum to a byte. Every use is a
  comparison or a switch (three sites).

Result on the test map: 45 KB of level geometry, peak zone use 1746 → 1702 KB,
free-at-worst 302 → 346 KB.

**How it was verified.** This touches collision detection, so "it still runs"
is not evidence. The host harness prints a hash of the final frame; the same
700-frame key script was run before and after the patch:

```
WITH the diet:    hash=018d226c   zonefree=793K min=346K
WITHOUT (before): hash=018d226c   zonefree=775K min=302K
```

**Byte-identical frames after 700 frames of walking, turning and shooting.**
A change in collision or geometry would have diverged within a few tics. Use
this method for every stage that follows — it is the difference between
refactoring DOOM and breaking DOOM quietly.

### RAM diet, stage 2 — done, also provably inert

Patch 0010 takes 16 bytes off `node_t` (52 → **36**):

`node_t.bbox[2][4]` is eight `fixed_t`, 32 of the structure's 52 bytes. But
**the WAD stores those as 16-bit shorts** and `P_LoadNodes` shifted them up by
`FRACBITS` — the low 16 bits were never anything but zero. Keeping them narrow
and expanding in `R_CheckBBox`, the only reader, is lossless by construction.
Four shifts per visited node against 16 bytes per node in the level.

Frame hashes again **byte-identical** across the 700-frame script. Peak zone use
1702 → 1680 KB, free-at-worst 346 → 368 KB.

Running total for the two stages: `line_t` 68 → 44 and `node_t` 52 → 36 on ARM,
level geometry for the test map **543 → 499 KB**, and nothing about the game
changed.

### The 68 KB wall, and where it came from

A crash reported from the watch: `failed on allocation of 68208 bytes`, and
only sometimes. That number identifies itself. DOOM1.WAD has exactly five lumps
of 68 168 bytes -- `WIMAP0`, `TITLEPIC`, `HELP1`, `HELP2`, `CREDIT` -- the
full-screen 320x200 graphics. Plus a 40-byte zone block header, that is the
request.

And DOOM asks for them at the worst possible moment. A level's `PU_LEVEL` data
is freed by the *next* `P_SetupLevel`, so when a level ends the intermission
loads `WIMAP0` while ~250 KB of dead geometry is still resident -- and when a
demo ends the attract loop loads `TITLEPIC` under the same conditions. On a PC
that is free real estate.

It is a *contiguous block* failure, not a shortage: dead level blocks
interleaved with cache leave no 68 KB hole however much cache the allocator
purges, because `Z_Malloc` merges only adjacent free blocks.

Patch 0013 releases the level at both sites (`UOOM_ReleaseLevel()`, which also
nulls `player_t::mo` first -- `doomgeneric_Tick` hands it to `S_UpdateSounds`
on every tick, intermission included).

**Measured, same 6 000-frame demo loop through E1M1, E1M5 and E1M7:**

| zone | before | after |
|---|---|---|
| 896 KB | dies on 68 208 | **passes** |
| 1 024 KB | passes | passes |

The host's floor drops from 1 024 KB to 896 KB, and frame hashes over a
700-frame scripted run are identical -- the fix changes when memory is
released, not what is drawn.

### The visplane number, and the bug under it

The first measurement said `MAXVISPLANES = 64` survived a 500-frame run of
Freedoom E1M1. It did not. It was corrupting the heap.

`R_FindPlane` bounds-checks before bumping `lastvisplane`. **`R_CheckPlane`
does not** -- not in vanilla, and doomgeneric inherited that. So an overflow
through the second path writes 664 bytes past the end of the array, straight
over `lastvisplane`, `floorplane` and `ceilingplane`, and reports nothing.
Upstream's post-hoc check in `R_DrawPlanes` fires only after the damage is
done.

Patch 0007 adds the guard Chocolate Doom uses. With it in place, `64` dies
immediately with `R_CheckPlane: no more visplanes` and `96` survives a
900-frame run. **The real floor was always 96; the earlier result was silent
memory corruption presenting as success.**

This is the argument for lowering these constants only alongside the guard.
`MAXVISPLANES` and `MAXOPENINGS` fail hard; `MAXDRAWSEGS` and `MAXVISSPRITES`
degrade visually. Know which is which before tuning.

Plus, outside patch 0003:

| Change | Saved |
|---|---|
| `DG_ScreenBuffer` never allocated (patch 0001) | **230-256 KB** |
| Screen wipes disabled (patch 0005) | ~192 KB of *zone peak* |
| `statdump.c` excluded | 6.4 KB |

The `DG_ScreenBuffer` removal is the largest single win in the port and it is
free: doomgeneric allocates `RESX * RESY * 4` to hold a 32-bit copy of a frame
that this panel wants at 8 bits per pixel. Nothing reads it once
`I_FinishUpdate` is hooked.

Measure the current static footprint yourself:

```sh
# host, with the zone shrunk so it does not dominate the number
make -C host clean && make -C host EXTRA_DEFS="-DUOOM_ZONE_BYTES=$((320*1024))"
size -m host/out/uoom-host           # macOS
# on the watch build, the only number that counts:
arm-none-eabi-size -A build/UOOMGUI.elf
arm-none-eabi-nm --size-sort -S build/UOOMGUI.elf | tail -30
```

## Ways out

In rough order of expected value.

**1. ~~Measure with `DOOM1.WAD`~~ — done.** 1024 KB floor, 571 KB peak, both
host figures. See the sweep above. This was worth a megabyte.

**2. ~~Raise `UNA_APP_GUI_RAM_LENGTH`~~ — no longer the question.** It is a
linker ceiling with no cap and no runtime cost, and the zone has moved off it
onto the kernel heap. What replaces this item is measuring the kernel heap with
`RequestMemoryInfo`.

**3. Use both processes.** Still available if the kernel heap disappoints: there
is **no MMU** ("the app can read entire MCU memory"), the two app regions live
in one address space, and `RequestDisplayUpdate` already passes a raw pointer
across the process boundary. Ugly, and now a fallback rather than a plan.

**4. Pre-flatten composite textures in a WAD converter.** Both of the ports
that actually run DOOM at 240x240 on a Cortex-M do this: an offline tool merges
multi-patch textures into single patches, which deletes `R_GenerateComposite`,
the 64 KB-per-texture composite cache, and the permanent per-texture
`texturecolumnlump`/`texturecolumnofs` side tables. next-hack reports it also
made rendering **3x faster**. Costs WAD size, which is on eMMC and free.

**5. Continue the structure diet.** Stage 1 is done (above). The remaining
stages, sized against the same map, in the order their value/risk ratio
suggests:

| stage | change | ARM saving, this map | risk |
|---|---|---|---|
| ~~2~~ | ~~`node_t.bbox`~~ — **done**, patch 0010, −22 KB | — | — |
| 3 | `seg_t.frontsector` / `backsector` removed. id's own comment on the identical fields in `line_t` reads *"Note: redundant? Can be retrieved from SideDefs."* For a seg they are `linedef->frontsector` / `backsector` swapped by a side bit, so one spare byte buys back eight. 32 → 24 bytes. | **−35 KB** | **medium** — `backsector` is read in `R_AddLine`, the hottest clipping decision in the renderer. Costs cycles to save bytes; measure the frame time. |
| 4 | `side_t.textureoffset` / `rowoffset`: `fixed_t` → `short`. Same provably-lossless argument as the node bboxes at load time — **but not at run time**: `p_spec.c`'s scrolling walls add to `textureoffset` every tic, so the field is not load-constant and narrowing it changes how a scroller wraps. Needs the units thought through, not just the type changed. 20 → 12 bytes. | **−32 KB** | medium — read per column in `r_segs.c`, written by scrollers |
| 5 | `mobj_t` 156 → ~52 bytes, plus a `static_mobj_t` for decorations that never move. This is next-hack's biggest single win: *"more than 30 kB of RAM in E1M6."* | −30 to −60 KB | **high** — touches everything |
| 6 | 16-bit indices instead of pointers throughout, 1-byte-overhead object pools, bitfield arrays instead of `boolean` arrays, zone header 24 → 8 bytes | large | high |

Stages 3 and 4 are worth ~67 KB more on this map. Stage 3 does not change
observable behaviour and can be verified by frame-hash equality; stage 4 can
change scroller behaviour, so it needs eyes as well as a hash. See
`docs/reference/prior-art.md` for the published numbers behind stages 5 and 6 --
this is a well-trodden path, not speculation.

**6. Attack the 247 KB blocks directly.** Level geometry is loaded whole. A
streaming `P_LoadNodes`/`P_LoadSegs` reading from the WAD on demand is real
work, but it is where the remaining megabyte is.

**7. The `mapped` pointer.** `wad_file_t` has a `mapped` field: set it and
**every lump access becomes a zero-copy pointer with no zone allocation at
all** (`w_wad.c`'s `W_CacheLumpNum` short-circuits). It is `NULL` in this tree
because no mmap backend exists. This would remove most of the cache half of the
zone -- but it needs the WAD to be memory-mapped, and the watch's bulk storage
is eMMC, which is a block device. It becomes available only if a trimmed WAD
can be placed in memory-mapped flash. Worth keeping in mind, not actionable
today.

**8. Render natively at 240x240** (`tools/apply-uoom-patches.py --native`).
Shrinks `visplane_t`, `MAXOPENINGS`, `ylookup`, `columnofs` and `I_VideoBuffer`
by 25%, at the cost of a clipped status bar. Maybe 25-30 KB of static, and a
faster frame. Not enough on its own, but it is nearly free.

## What is *not* a way out

Reducing the zone below the floor. DOOM does not degrade when the zone is
tight -- `Z_Malloc` calls `I_Error` and the game stops. There is no
"low memory mode" to fall back on.

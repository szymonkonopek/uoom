# Prior art: DOOM on very constrained MCUs

Everything below is published work by other people. It matters to UOOM for one
reason: **a 240x240 DOOM has already been made to fit in 256 KB of RAM**, so
the memory problem in `docs/03-memory-budget.md` is not a wall, it is a
to-do list with known answers.

Two ports are directly relevant, one for the techniques and one for the
hardware.

---

## next-hack's ports — the closest match to this hardware

**nRF52840Doom** — Cortex-M4 @64 MHz, **256 KB RAM**, 1 MB internal flash,
16 MB QSPI, **240x240 ST7789**. Same resolution as the UNA Watch.
<https://next-hack.com/index.php/2021/11/13/porting-doom-to-an-nrf52840-based-usb-bluetooth-le-dongle/> ·
<https://github.com/next-hack/nRF52840Doom>

| | |
|---|---|
| 3D view | 240x208 (32-px status bar) — 92% of vanilla's pixel count |
| **Zone memory** | **75–113 KB** |
| Framebuffers | ~112 KB (two 240x240 8-bit) |
| Frame rate | 34.5 fps cap; E1M1 34.5, worst case ~22 |

**MG24_Doom_BLE** — EFR32MG24, **Cortex-M33 @80 MHz**, 256 KB RAM, 1.5 MB
internal flash. This is UOOM's exact core class.
<https://next-hack.com/index.php/2023/12/10/multiplayer-doom-on-the-sparkfun-thing-plus-matter-board/>

- 320x240 or 320x200, **30–35 fps**, SPI-limited to 32.6 fps.
- The double-buffered 320x240 8bpp framebuffer alone is **150 kB — ~60% of all
  RAM**, leaving ~104 kB for game, audio, music and BLE.
- **Two interleaved SPI flashes for ~10 MB/s by DMA, explicitly *not*
  memory-mapped.** This is the important one for UOOM: zero-copy XIP is not
  required. Texture and sprite **columns are DMA-fetched while the CPU renders
  the previously fetched column**, and patch headers are rewritten by the WAD
  converter to carry column length in bytes so one column is one DMA transfer.

### The techniques, with their published savings

| Technique | Effect |
|---|---|
| **Pre-flatten composite textures offline** | deletes `R_GenerateComposite`, the 64 KB-per-texture cache and the per-texture side tables; **3x faster** rendering |
| `mobj_t` 140 → 92 → **52 bytes** | the single biggest structure win |
| `static_mobj_t`, 44 bytes, for immovable decorations | "**more than 30 kB of RAM in E1M6**" |
| Three object classes: fully static / partially static / regular | splits constant from variable fields |
| **16-bit "short pointers"** into the arena | halves every internal reference |
| Object pools, **1 byte** of overhead | vs. per-allocation headers |
| Zone allocator header **28 → 8 bytes** | across thousands of allocations |
| Z-coordinates as **13.3** instead of 16.16 fixed point | precision nobody sees |
| `boolean` arrays → bitfield arrays (including `validcount`) | 32x on flags |
| Cache flats, colormaps, sprite defs and the lump-name table into internal flash at level load | 20–30 s level load, but the hot set never touches RAM |
| OPL2 emulated at 11 025 Hz instead of 49 716 | irrelevant here — no audio path |

Speed, for reference: full framebuffer double-buffering with DMA was **+12 fps**
over single-buffering; the floor/ceiling loop packs **4 pixels per register**
(one store instead of four); hand-pipelined assembly took the 8→16-bit line
conversion from ~2200 to ~1000 cycles per line.

The ancestor of the family is **MG21DOOM** on an IKEA TRÅDFRI lamp (EFR32MG21,
**108 KB** usable): "RAM usage was well beyond the 108 kB limit... close to
160 kB". The `static_mobj_t` idea comes from there.
<https://github.com/marciopocebon/MG21DOOM>

---

## rp2040-doom — the reference work on RAM reduction

Graham Sanderson, RP2040 @270 MHz, **264 KB RAM**, 320x200 @ 60 Hz VGA,
**30–35+ fps**. The techniques are in the six-part write-up, not the README.
<https://kilograham.github.io/rp2040-doom/> ·
**RAM chapter:** <https://kilograham.github.io/rp2040-doom/speed_and_ram.html>

> *"A straight RP2040 compile of the Chocolate Doom source code off which
> RP2040 Doom is based requires **300K of static mutable data** and a minimum of
> about another **700K of Doom 'zone memory'**."*

Result: total budget **266.25 K**, with the 700 K zone reduced to *"only
consumes up to about **45K** depending on the level"*. Of the RAM, *"the
majority, almost 180K, is related to the display."*

### The RAM-reduction list (his own headings)

- **Move static data into flash** — a `should_be_const` typedef threaded
  through the code, `const` in the RP2040 build.
- **Use smaller data types**; **reduce field precision** — 16:16 fixed point
  stored as just the integer 16 bits, or as **14:2** for floor/ceiling heights.
- **Reuse one static buffer for multiple purposes**; **booleans → bit-sets**.
- **16-bit pointers** into the heap; **16-bit indexes** rather than pointers.
- **Reorder structures to pack** — smallest fields first, for cheap base-pointer
  addressing on Cortex-M0+.
- **Singly-linked** thinker/mobj lists (*"often does cause 'desync' in
  demos"*).
- **Runtime flags → `#define`s** — *"huge knock-on benefit of removing now
  unreachable code paths."* No runtime screen-size change, so every
  screen-size LUT becomes `const` in flash.
- **Zone overhead 20 → 8 bytes**, plus **object pools**: one spare header byte
  holds 8 "slot allocated" flags, so **8 small objects per zone block**.
- **Sub-classify objects** — static vs full `mobj_t`; a decoration's
  coordinates live in flash with the level data.
- **Don't instantiate level data in RAM. Don't instantiate texture metadata in
  RAM.** Works directly off a compressed flash representation.
- **Reorder WAD data to shrink mutable fields** — switch textures renumbered to
  be adjacent so a switch is a 1-bit XOR; animated textures renumbered to very
  low indices.
- **Remove drawsegs and visplanes** (see below).
- **20 K saved by storing the sine table as 16-bit** — but *"reflecting the
  quarter cycles of the sin table... caused desync even though the errors are
  at most 1/65536!"*

### The renderer, and what it says about visplanes

BSP traversal emits columns into **320 per-x linked lists** of non-overlapping,
y-sorted columns, so each texture column is decompressed **once per frame** and
rendering can happen in any order.

> *"The size of this column list metadata in RP2040 Doom is **3600 columns at 12
> bytes each (i.e. 42K)** ... however the use of the column lists actually
> allows us to remove the oft-maligned 'visplane' and 'drawseg' structures used
> by vanilla Doom rendering code which are themselves **about 70K big**."*

Visplanes survive only as an *index*: floor/ceiling pixels are written with the
visplane index as their value, using a **9th framebuffer bit** to tell index
from pixel; a second pass finds runs and draws spans grouped so **each 64x64
flat is decompressed once per frame**.

Framebuffers are **320x168** — *"I didn't have enough RAM for the extra 10K per
frame-buffer for the last 32 lines"* — and the 32-px status bar has **no
framebuffer at all**: it is an overlay display list drawn at scan-out, with its
background streamed from flash every frame.

### WHD: the WAD, halved

`DOOM1.WAD` **4098 K → 1758 K (57%)**, *"basically lossless except for the
sound effects."* Constraint: **all compressed data must be randomly accessible
directly in flash**. **No LZ77** — *"RP2040 Doom needs to be able to encode and
decode really short (10s of bytes) sequences of data, so the dictionary method
is a bad choice."* Instead: packed bit-fields, variable-byte integers, and
Huffman with format-specific code-length packings.

Highlights worth stealing:

- **Palettes: 97%.** The WAD's 33 tinted copies of PLAYPAL are *omitted* and
  the tint is recomputed from the 768-byte base at most once per frame.
- **Patches:** *"the palette is ordered such that lighter and darker versions of
  the same base color are at adjacent palette indexes"* — so seven extra
  symbols encode a delta of −3..+3 from the previous index in the column.
  **"Saves another 130K or so."**
- Pixel data decodes **forward** from `columnofs[x]` while post metadata decodes
  **backward** from `columnofs[x+1]` — one offset, two streams, no lengths.
- **SideDefs 90%:** *"there are actually only **16 possible patterns** of
  texture values... I just store the 4-bit pattern selector."*
- **LineDefs:** back side *"is always either 65535 or 'Front Side' + 1... and
  thus that field can actually be encoded in a **single bit**!"*
- **Nodes:** bounding boxes from 4x16-bit to **4x4-bit** on a grid over the
  parent box — legal because *"it doesn't matter if our bounding boxes are
  slightly too large."*
- **Savegames ~90%:** *"the RP2040 Doom saved game format [is] a **'diff' from
  the state when the level is loaded**."*

The warning to carry forward: *"**Some of the levels in these later games can
occasionally run out of space for the rendering data structures, causing some
areas of the screen to become black**"* — and the consolation: *"the demos turn
out to be really good regression tests!"*

---

## How other ports get at the WAD — four architectures

**(A) Lazy reads into the zone.** What unmodified DOOM already does:
`W_CacheLumpNum` allocates on a miss and `W_ReleaseLumpNum` retags to
`PU_CACHE`, which `Z_Malloc` purges under pressure. **There is no separate LRU
— "the lump cache" is the zone heap with purgeable tags.** This is UOOM's
current architecture.

**(B) FatFs behind (A).** `floppes/stm32doom` patches `w_file_stdc.c` so
`fopen` becomes `f_open` and reads become `f_lseek` + `f_read`. No extra cache
layer; every `W_Read` is a fresh seek. Proven to work with WADs larger than
available RAM. <https://github.com/floppes/stm32doom>

**(C) A fake filesystem over memory-mapped flash.** ~50 lines: `f_open`
compares the path to a constant and `f_read` is a `memcpy` from a `const`
array in XIP flash. <https://github.com/ghidraninja/game-and-watch-doom>

**(D) Zero-copy pointers into flash — the GBADoom lineage, and what makes
sub-256 KB ports possible.** No file layer at all:
`W_CacheLumpNum(lump)` → `return &doom_iwad[l->filepos];`. Lumps are **never
copied into RAM**. Requires an offline converter that pre-expands composite
textures. <https://github.com/doomhack/GBADoom>

**No MCU DOOM port implements an explicit LRU lump cache.** (A) and (D) are the
only two patterns in the wild — which is why `uoom_file.c` deliberately does
not have a block cache.

For UOOM specifically: eMMC is a block device, so (D) is unavailable as-is. The
route that fits is next-hack's: **copy the hot subset into internal flash at
level load and stream columns from eMMC by DMA.**

## Other ports, briefly

| Port | Hardware | Notes |
|---|---|---|
| `NordicPlayground/nrf-doom` | nRF5340, **Cortex-M33 @128 MHz** | 320x200 at **30–36 fps**; 8 MiB QSPI WAD via **XIP**, no RAM copies; composite textures pre-generated |
| `rota1001/stm32h7-baremetal-doom` | STM32H750 | **multi-zone allocator** — *"extend the original zone implementation to support non-contiguous memory zones"*, 4 banks. Directly relevant if UOOM ends up using both process RAM regions |
| `bane9/STM32DISCOVERY_DOOM` | STM32F429 + 8 MB SDRAM | doomgeneric, *"all of the official doom iwads"* from FAT32 — proof of lazy reads beyond RAM size |
| `ghidraninja/game-and-watch-doom` | STM32H7B0, 1 MB RAM | WAD is **fraggle's `miniwad`**, a Doom-II-compatible IWAD *under a quarter of a megabyte*, linked into flash |
| `Spritetm/esp32c3-doom-bauble` | ESP32-C3, **400 KB, no PSRAM** | works only because GBADoom keeps the WAD in memory-mapped flash |
| `unlimitedbacon/TTGO-DOOM` | LilyGO T-Watch-2020, **1.54" 240x240** | the only other MCU *smartwatch* DOOM; PrBoom, WAD in a flash partition. A tap posts **both `KEYD_ENTER` and `KEYD_RCTRL`** — *"Double event. Send menu select and also shoot"* |
| `twstokes/DarwinDOOM` | Apple Watch | doomgeneric; *"very few tweaks needed in doomgeneric itself"* |
| jborza's "DOOM on a watch" | — | **not a port** — dithered frames over serial at ~7 fps |

Pebble is documented as infeasible (256 kB of resources per app vs a 4.1 MB
IWAD). No PineTime or Bangle.js port exists.

## Control schemes with very few inputs

The two techniques that actually solve it: a **modifier layer**, and **one input
emitting two DOOM keys**.

**next-hack's ALT layer** (8 keys → 13 actions): holding ALT shifts meanings —
Left/Right become strafe, USE becomes menu, weapon-up becomes weapon-down, FIRE
becomes automap. Their own retrospective on later moving to 16 buttons: *"the
previous version required button combinations for functions like menu, map, and
strafe due to limited inputs."*

**GBADoom** — the cleanest few-button design found, and three ideas worth
stealing verbatim:

```c
const int key_use   = KEYD_A;
const int key_speed = KEYD_A;          // one button is both
// Use button negates the always run setting.
speed = (_g->gamekeydown[key_use] ^ _g->alwaysRun);
...
if (gamekeydown[key_use] && gamekeydown[key_straferight]) {
    newweapon = P_WeaponCycleUp(&_g->player);
    side -= sidemove[speed];  // Hack cancel strafe.
}
```

One button is Use *and* Run; run is `use XOR alwaysRun`, so with always-run on,
*holding* the button walks; and the weapon chord **cancels its own strafe
contribution** so you do not sidestep while switching. Cheats are Konami-style
button sequences.

**Playdate** assigns the arrows to *strafe* and the crank to *turning* — the
opposite of the naive mapping, and the right choice with one analog axis.

**Rockbox on iPod** makes `UP` and `OPEN` the same key, so walking forward
continuously presses use: **de facto auto-use, doors open by walking into
them.** There is no backward key at all.

**Nintendo Alarmo** (one dial, two buttons) is the most extreme real mapping:
dial → analog turning, dial press → forward, one button → **FIRE and ENTER
together**, other → USE. No backward, no strafe, no weapon switch, no ESC.

Engine-level auto-features worth borrowing (Doom Retro cvars): `autouse`
(*"automatically using doors and switches in front of you"*), `autofire`,
`autoaim`, `autoswitch`.

**Backward is the action ports sacrifice** — Alarmo, iPod-Rockbox and DOOM GB
all ship without it. UOOM keeps it, on the one safe chord.

## What this means for UOOM

1. **240x240 DOOM in ~256 KB is proven** — twice, by the same author, once on
   this exact core class. The 2 MB figure in `docs/03-memory-budget.md` is the
   cost of running *unmodified* engine data structures, not a floor.
2. **The order of attack is settled by prior art**: pre-flatten composites
   offline, then shrink `mobj_t`, then short pointers and pools, then stream
   columns by DMA. All published, all with numbers.
3. **Do not shrink `SCREENWIDTH`.** 240 < 320 breaks `STBAR` and every
   full-screen patch on frame 2 (`Bad V_DrawPatch ... patch.width=320`), and
   fixing it properly means re-authoring the UI art, as GBADoom did.
   rp2040-doom's approach is better: keep the logical width and take the
   status-bar rows out of the framebuffer. UOOM's `--native` patch exists but
   is opt-in for exactly this reason.

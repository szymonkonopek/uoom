# doomgeneric internals, as they bear on this port

Findings against `ozkl/doomgeneric` at pinned commit `dcb7a8d`. Sizes were
obtained by compiling a `sizeof` probe against the real headers for
`armv8m.main-none-eabi` with soft float (ILP32, 4-byte pointers,
`boolean` = `int` = 4 bytes), not estimated. `file:line` references are to
`third_party/doomgeneric/doomgeneric/`.

## The DG_ platform API

`doomgeneric.h:38-43` is the entire contract.

| Function | Called from | Must do |
|---|---|---|
| `void DG_Init(void)` | `doomgeneric.c:23`, before `D_DoomMain()` | Bring up display and input. Runs **before `Z_Init`**, so no zone allocation is available yet. |
| `void DG_DrawFrame(void)` | `i_video.c:369`, last statement of `I_FinishUpdate()` | Present. Also called repeatedly inside the melt-wipe loop (`d_main.c:327`). |
| `void DG_SleepMs(uint32_t)` | `i_timer.c:81` (`I_Sleep`) | Block. Callers include the wipe loop's `I_Sleep(1)`. |
| `uint32_t DG_GetTicksMs(void)` | `i_timer.c:40` | Free-running ms. `I_GetTime` = `(ticks*TICRATE)/1000`, `TICRATE 35` (`i_timer.h:23`). `ticks*35` overflows uint32 at ~34 h of uptime; `basetime` is captured on first call (`i_timer.c:49-52`). |
| `int DG_GetKey(int *pressed, unsigned char *key)` | `i_input.c:286` | Pop one event, 0 when empty. `*key` is **already a doomkeys.h value** — `TranslateKey` is the identity (`i_input.c:225-228`). |
| `void DG_SetWindowTitle(const char*)` | `i_video.c:470` | May be empty. |

Loop: `doomgeneric_Create(argc, argv)` once, then `doomgeneric_Tick()` forever.
`doomgeneric_Tick` (`d_main.c:405-419`) is
`I_StartFrame(); TryRunTics(); S_UpdateSounds(); if (screenvisible) D_Display();`

## The trap at 240 pixels wide

`i_video.c:282-284`:

```c
fb_scaling = s_Fb.xres / SCREENWIDTH;            // 240/320 == 0
if (s_Fb.yres / SCREENHEIGHT < fb_scaling) ...   // 1 < 0, false
```

`fb_scaling` becomes **0**, so the copy loop in `I_FinishUpdate`
(`i_video.c:345`) never executes and the screen stays black. **The stock
`I_FinishUpdate` cannot be used on this panel** — which is convenient, because
we want to replace it anyway.

Also note `DOOMGENERIC_RESX/RESY` are **not** DOOM's render resolution.
`SCREENWIDTH 320` / `SCREENHEIGHT 200` are fixed in `i_video.h:27-28`, and
RESX/RESY have exactly two consumers: the `DG_ScreenBuffer` malloc
(`doomgeneric.c:21`) and `s_Fb` (`i_video.c:211-212`).

## The 8bpp → 32bpp conversion we delete

`i_video.c:156-203` (`cmap_to_fb`) does a palette lookup and a 32-bit store per
pixel; `i_video.c:321-370` drives it per row. Cost at 320×200: 64 000 lookups,
64 000 stores, and a 256 000-byte intermediate buffer.

`DG_ScreenBuffer` is `malloc(DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4)` —
**unconditionally `*4`**, even in `CMAP256` mode where three quarters is waste.
Nothing else in the engine reads it once `I_FinishUpdate` is hooked, so it can
stay `NULL`.

`I_SetPalette` (`i_video.c:388-420`) fills `colors[256]` (1024 B) from
`gammatable[usegamma]`, which is `const` and lives in flash (`tables.c:2129`).
`rgb565_palette[256]` (512 B) is dead except in `I_GetPaletteIndex`, itself
only reached from `V_DrawMouseSpeedBox`.

## Screen buffers (8bpp, 64 000 B each at 320×200)

| Buffer | Site | Bytes | Lifetime |
|---|---|---|---|
| `I_VideoBuffer` | `i_video.c:290`, `Z_Malloc(PU_STATIC)` | 64 000 | whole run |
| `background_buffer` | `r_draw.c:846` | 53 760 | only when the view is windowed |
| `wipe_scr_start` | `f_wipe.c:237` | 64 000 | per wipe |
| `wipe_scr_end` | `f_wipe.c:249` | 64 000 | per wipe |
| `wipe_shittyColMajorXform` temp | `f_wipe.c:52` | 64 000 | transient, twice, inside `wipe_initMelt` |
| melt `y[]` | `f_wipe.c:150` | 1 280 | per wipe |
| `wipe_scr` | `f_wipe.c:276` | 0 | **aliases `I_VideoBuffer`** |

`V_Init` is a **no-op** (`v_video.c:597-602`) — the classic `screens[4]` array
is gone; `dest_screen` is retargeted by `V_UseBuffer`/`V_RestoreBuffer`.

Peak wipe-time footprint is therefore ~256 KB of zone. Disabling wipes removes
~192 KB of that peak: the cheapest large saving in the port.

## Largest static arrays (computed, ARM32, 320×200)

| Symbol | Site | Constant | Elem | **Bytes** |
|---|---|---|---|---|
| `visplanes` | `r_plane.c:46` | `MAXVISPLANES 128` | `visplane_t` **664** | **84 992** |
| `openings` | `r_plane.c:53` | `SCREENWIDTH*64` = 20 480 | `short` | **40 960** |
| `states` | `info.c:127` | `NUMSTATES` 967 | 28 | **27 076** (.data) |
| `ticdata` | `d_loop.c:60` | `BACKUPTICS 128` × `NET_MAXPLAYERS 8` | 160 | **20 480** |
| `viewangletox` | `r_main.c:93` | `FINEANGLES/2` 4096 | 4 | **16 384** |
| `mobjinfo` | `info.c:1098` | 137 | 92 | **12 604** (.data) |
| `drawsegs` | `r_bsp.c:46` | `MAXDRAWSEGS 256` | 48 | **12 288** |
| `zlight` | `r_main.c:102` | 16×128 | ptr | **8 192** |
| `vissprites` | `r_things.c:281` | `MAXVISSPRITES 128` | 60 | **7 680** |
| `captured_stats` | `statdump.c:57` | 32 | 200 | **6 400** |
| `S_sfx` | `sounds.c:116` | 109 | 48 | **5 232** (.data) |
| `columnofs` | `r_draw.c:64` | `MAXWIDTH 1120` | 4 | **4 480** |
| `ylookup` | `r_draw.c:63` | `MAXHEIGHT 832` | ptr | **3 328** |
| `scalelight` | `r_main.c:100` | 16×48 | ptr | **3 072** |
| `m_config` defaults | `m_config.c:111,684` | 149 | 24 | **3 576** (.data) |

**Static total ≈ 280 KB**, of which `visplanes` + `openings` is 45%.

`states` and `mobjinfo` must stay **writable**: `g_game.c:1815-1826` mutates
`states[i].tics` and `mobjinfo[MT_*].speed` for fast monsters and nightmare
skill. `const`-ing them is only safe if fast monsters are dropped.

Correctly in flash (all `const`, `tables.h:49-87`): `finetangent` 16 KB,
`finesine` 40 KB, `tantoangle` 8 KB, `gammatable` 1.25 KB.

## The zone

```c
// i_system.c:58-59
#define DEFAULT_RAM 6 /* MiB */
#define MIN_RAM     6  /* MiB */
// i_system.c:119
zonemem = malloc(*size);
```

`AutoAllocMemory` (`i_system.c:95-131`) tries 6 MiB and steps down 1 MiB at a
time, calling `I_Error` below `MIN_RAM`. Per-block `memblock_t` header is 24 B
(`z_zone.c:39-47`), `MEM_ALIGN` is 4, `MINFRAGMENT` 64.

## File I/O — `w_file` is the clean seam

```c
// w_file.h:28-44
typedef struct {
    wad_file_t *(*OpenFile)(char *path);
    void (*CloseFile)(wad_file_t *file);
    size_t (*Read)(wad_file_t *file, unsigned int offset,
                   void *buffer, size_t buffer_len);
} wad_file_class_t;

// w_file.h:46-60
struct _wad_file_s {
    wad_file_class_t *file_class;
    byte *mapped;            // NULL => not memory-mapped
    unsigned int length;
};
```

Three functions, **absolute-offset reads** (no seek state), 12-byte handle.
`W_OpenFile` (`w_file.c:53-83`) goes straight to `stdc_wad_file.OpenFile`
unless `-mmap` is given, and `HAVE_MMAP` is `#undef` (`config.h:40`), so
providing a `wad_file_class_t` under the name `stdc_wad_file` replaces the
backend with **no edit to `w_file.c` at all**.

`W_AddFile` decides WAD-vs-lump with `strcasecmp(filename+strlen-3, "wad")`
(`w_wad.c:163`), reads a 12-byte header at offset 0, then the directory at
`infotableofs` (`w_wad.c:204`) in **one** read — 46 704 B for doom2.wad. A
backend must tolerate a large single read or chunk it internally.

### Lumps are strictly lazy

`W_CacheLumpNum` (`w_wad.c:382-422`):

```c
if (lump->wad_file->mapped != NULL)  result = lump->wad_file->mapped + lump->position;
else if (lump->cache != NULL)        { result = lump->cache; Z_ChangeTag(...); }
else { lump->cache = Z_Malloc(W_LumpLength(lumpnum), tag, &lump->cache);
       W_ReadLump(lumpnum, lump->cache); result = lump->cache; }
```

One `W_Read` per lump miss. `mapped` is **always NULL** in this tree — but
setting it to a flash base address would make **every lump access a zero-copy
pointer with no zone allocation at all**. That is the biggest RAM win available
to any port that can memory-map its WAD.

`W_ReleaseLumpNum` only retags to `PU_CACHE`; `Z_Malloc` purges those on
demand. So a zone that is too small does not crash — it turns into an I/O
storm, re-reading lumps it just discarded.

`lumpinfo` is `calloc(numlumps, 28)` (`w_wad.c:92`) and never freed:
doom1.wad 1264 lumps → 35 KB; doom.wad 2306 → 65 KB; doom2.wad 2919 → 82 KB.

### Remaining stdio, and where

| Site | Call | Note |
|---|---|---|
| `w_file_stdc.c:38,50,62,79,83` | `fopen`/`M_FileLength`/`fclose`/`fseek`/`fread` | the WAD — replaced wholesale |
| `m_misc.c:55-62` | `mkdir` | `M_MakeDirectory`, called unconditionally at startup from `M_SetConfigDir` (`m_config.c:2079`) and `M_GetSaveGameDir` (`:2120`) — **must be stubbed** |
| `m_misc.c:66-84` | `fopen` + `errno == EISDIR` | `M_FileExists`, how `d_iwad` finds the IWAD |
| `g_game.c:1554-1676` | `fopen`/`ftell`/`fclose`/`remove`/`rename` | savegames, live |
| `p_saveg.c:39,85,101,156,172` | `FILE*`, `fread`/`fwrite`/`ftell` | the serialiser is **byte at a time** |
| `m_menu.c:513-521` | `fopen`/`fread`/`fclose` | `M_ReadSaveStrings`, runs when the Load menu opens |
| `v_video.c:731,783` | `fopen` | PCX screenshot |
| `m_argv.c`, `m_config.c` read/write, `statdump.c`, `i_endoom.c` | — | **already dead**: `#if ORIGCODE` and `config.h:97` has `#undef ORIGCODE` |
| ~95 `printf` | — | route or `#define` away |

`SAVEGAMESIZE` is `0x2c000` = 180 224 B (`g_game.c:76`) — too large to buffer
in RAM on this target, so savegames must stream.

## Modules to stub or exclude

`doomfeatures.h:22-37` — note the last line is *commented out*, so
`FEATURE_SOUND` is simply never defined anywhere:

```c
#undef FEATURE_WAD_MERGE
#undef FEATURE_DEHACKED
#undef FEATURE_MULTIPLAYER
//#undef FEATURE_SOUND
```

| Subsystem | Status |
|---|---|
| Digital sound | Already off. `i_sound.c:74-80` compiles `sound_modules[] = { NULL }` and every `I_*` entry point NULL-checks. **There is effectively a built-in null driver** — keep `i_sound.c` + `s_sound.c`, cost is 96 B of channels plus 6.3 KB of `S_sfx`/`S_music` tables. Zero work. |
| Music | `memio.c` and `mus2mid.c` are reachable only from music code — drop both. `dummy.c:43-49` already stubs `I_InitTimidityConfig`. |
| Networking | No `net_*.c` exist. Keep `d_net.c` + `d_loop.c` (they hold the tic loop); `dummy.c:27-29` supplies `net_client_connected` and `drone`. |
| Joystick, CD audio, ENDOOM, statdump | Bodies are all `#ifdef ORIGCODE` → already empty. Droppable; `statdump.c` is worth 6.4 KB. |
| `m_config.c` | Read and write are both dead. What remains live and matters is the `M_MakeDirectory` call. Dropping the file also removes the only `atof`/`"%f"` in the build. |
| `i_scale.c` | 32 KB of source for stretch tables this `i_video.c` never uses. Drop. |

## libc dependencies

| Symbol | Uses | Risk on newlib-nano |
|---|---|---|
| `printf` | 95 | Retarget or `#define` away. Formats are `%i/%s/%d/%x/%p/%c` — **no `%f`** outside dead code. |
| `malloc` outside the zone | `doomgeneric.c:21`, `i_system.c:77,119`, `m_config.c:2045`, `p_saveg.c:70`, `m_misc.c:295,451`, `calloc` at `w_wad.c:92` | Needs a small real heap (~8 KB) or a static pool. |
| `strcasecmp` / `strncasecmp` | 9 / 7 | **Not reliably in newlib-nano**, pulled from `<strings.h>` via `doomtype.h:35`, and in the WAD lookup hot path (`w_wad.c:273,287`). Provide your own. |
| `vsnprintf` | 5 | The real hazard. Keep `-u _printf_float` off; grep confirms no `%f` reaches it. ~3-5 KB of flash. |
| `sscanf` | 6 | `M_StrToInt` (`m_misc.c:192-195`); drags in a second formatter. Trivially replaceable with `strtol`. |
| `atof` | 1 | `m_config.c:1766` — pulls in the float parser. Goes away with `m_config.c`. |
| `exit` | 21 | Redefine. |
| `setjmp` / `longjmp` / `alloca` / `atexit` | **0** | None. `I_AtExit` is DOOM's own list. |
| `time` / `localtime` / `clock` | **0** | Everything goes through `DG_GetTicksMs`. |
| `qsort` / `rand` | **0** | `m_random.c` is a 256-byte table. |
| `sin`/`cos`/`tan`/`sqrt`/`pow` | **0 live** | `r_main.c:434,528` are inside `#if 0`. **Links with no libm.** |
| `fabs` | 1 | `v_video.c:868`, only reached with `-testcontrols`. Removing `V_DrawMouseSpeedBox` eliminates all soft-float from the renderer. |

**Where the frame time will actually go:** `FixedDiv` uses a 64-bit divide, and
`r_segs.c` / `r_plane.c` / `R_ScaleFromGlobalAngle` call it per column.
Cortex-M33 has hardware 32/32 `SDIV` but 64/32 goes to `__aeabi_ldivmod`.
Compile the renderer `-O2` even if the rest is `-Os`.

## The key-queue bug worth knowing about

`I_GetEvent` (`i_input.c:279-324`) `break`s out of its drain loop after the
**first key-up** it sees in a tic. Combined with a 16-slot ring that overwrites
its oldest entry, a device that emits press+release pairs quickly can strand a
key in the down state — the player walks into a wall until restart. UOOM
removes that `break` (patch 0004) and uses a 64-slot power-of-two ring.

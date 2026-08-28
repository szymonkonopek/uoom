/* uoom_config.h -- compile-time configuration for the UOOM port of DOOM
 *
 * Every knob that trades memory, speed or fidelity lives here. Nothing in the
 * port layer hardcodes a screen size, a pixel layout or a control mapping.
 *
 * See docs/03-memory-budget.md for the numbers behind the defaults.
 */
#ifndef UOOM_CONFIG_H
#define UOOM_CONFIG_H

/* ------------------------------------------------------------------ display */

/* Physical panel of the UNA Watch. The kernel owns a static 240x240x1B
 * framebuffer in ABGR2222; see docs/04-video.md. */
#define UOOM_PANEL_W            240
#define UOOM_PANEL_H            240
#define UOOM_PANEL_PITCH        UOOM_PANEL_W
#define UOOM_PANEL_BYTES        (UOOM_PANEL_W * UOOM_PANEL_H)

/* Internal resolution DOOM renders at.
 *
 * Note what DOOMGENERIC_RESX/RESY are *not*: they do not set DOOM's render
 * resolution. SCREENWIDTH/SCREENHEIGHT are hardcoded 320x200 in
 * doomgeneric/i_video.h, and RESX/RESY only size doomgeneric's 32-bit output
 * buffer, which this port does not use at all. Rendering at anything other
 * than 320x200 therefore means patching i_video.h -- see
 * tools/apply-uoom-patches.py --native.
 *
 *   UOOM_RENDER_SCALED (default)
 *     DOOM renders its native 320x200 -- the resolution every status bar,
 *     menu and intermission graphic in the WAD was authored for -- and the
 *     video layer resamples to the panel.
 *
 *   UOOM_RENDER_NATIVE  (requires --native when vendoring DOOM)
 *     SCREENWIDTH/SCREENHEIGHT become 240x240. No resample, and every
 *     width-dependent renderer array shrinks: visplane_t carries top[] and
 *     bottom[] of SCREENWIDTH bytes each and MAXOPENINGS is SCREENWIDTH*64,
 *     so this is worth ~25KB on its own. The cost is a clipped status bar and
 *     some clipped menu art.
 */
#define UOOM_RENDER_SCALED      0
#define UOOM_RENDER_NATIVE      1

#ifndef UOOM_RENDER_MODE
#define UOOM_RENDER_MODE        UOOM_RENDER_SCALED
#endif

#if UOOM_RENDER_MODE == UOOM_RENDER_SCALED
#  define UOOM_DOOM_W           320
#  define UOOM_DOOM_H           200
#else
#  define UOOM_DOOM_W           UOOM_PANEL_W
#  define UOOM_DOOM_H           UOOM_PANEL_H
#endif

/* How the 320x200 image lands on the panel.
 *
 * The panel is addressed as 240x240, but the LS012B7DD06A's active area is a
 * *circle* of radius 120 inscribed in that square -- the corners are not
 * visible, and the datasheet's per-line tables confirm it (line 240 is
 * 109 dummy / 22 active / 109 dummy). At row 230 only about 96 of the 240
 * columns are lit. That is the constraint this setting exists for.
 *
 *   FILL  -- stretch to the full 240x240 square. Worth being precise about why
 *            this is not as wrong as it looks: DOOM's 320x200 was displayed on
 *            a 4:3 CRT, so its pixels were tall (1:1.2), which makes scaling
 *            200 rows up to 240 the historically *correct* vertical geometry.
 *            The horizontal 0.75 squeeze is the part that is wrong.
 *            Default: the 3D view is centre-weighted and survives the circle
 *            well. The status bar does not -- see docs/04-video.md.
 *   FIT   -- 240x150 letterboxed. Faithful geometry, and the blank rows land
 *            exactly where the circle was going to clip anyway. Costs 37% of
 *            an already tiny screen.
 *   INSCRIBED -- fit the whole frame inside the visible circle: a 169x169
 *            centred square, nothing clipped, ever. The honest option if the
 *            status bar matters, and the one to use for screenshots that must
 *            match what a wearer sees.
 */
#define UOOM_SCALE_FILL         0
#define UOOM_SCALE_FIT          1
#define UOOM_SCALE_INSCRIBED    2

/* Radius of the panel's visible circle, in pixels. */
#ifndef UOOM_PANEL_RADIUS
#define UOOM_PANEL_RADIUS       120
#endif

#ifndef UOOM_SCALE_MODE
#define UOOM_SCALE_MODE         UOOM_SCALE_FILL
#endif

/* ABGR2222 bit layout: 8 bits per pixel, 2 bits per channel.
 * Overridable in one place so a wrong guess is a one-line fix. */
#ifndef UOOM_PIX_A_SHIFT
#define UOOM_PIX_A_SHIFT        6
#define UOOM_PIX_B_SHIFT        4
#define UOOM_PIX_G_SHIFT        2
#define UOOM_PIX_R_SHIFT        0
#endif
#define UOOM_PIX_OPAQUE_A       3u   /* alpha == 3 -> fully opaque */

/* Dithering when collapsing DOOM's 256-colour palette into 2 bits/channel.
 * 0 = nearest (fast, banded), 1 = ordered 2x2 Bayer on the panel grid.
 * See docs/04-video.md -- 64 colours is the single biggest fidelity loss in
 * this port, and dithering buys back a surprising amount of it. */
#ifndef UOOM_DITHER
#define UOOM_DITHER             1
#endif

/* ------------------------------------------------------------------- timing */

/* Kernel GUI tick is 30-60Hz; DOOM's logic runs at a fixed 35Hz (TICRATE).
 * We never render faster than the panel can be pushed. */
#ifndef UOOM_TARGET_FPS
#define UOOM_TARGET_FPS         20
#endif

/* ------------------------------------------------------------------ features */

#ifndef UOOM_ENABLE_SOUND
#define UOOM_ENABLE_SOUND       0    /* no audio DAC on the watch */
#endif

#ifndef UOOM_ENABLE_MUSIC
#define UOOM_ENABLE_MUSIC       0
#endif

/* Hold DOOM's run modifier permanently. On a 240px screen you want to cover
 * ground, and there is no button to spare for a walk/run toggle. */
#ifndef UOOM_AUTORUN
#define UOOM_AUTORUN            1
#endif

/* Print each level-geometry array's size at load. The zone floor is set by
 * these arrays, so this is how a structure diet gets measured. */
#ifndef UOOM_LOG_MAP_ALLOC
#define UOOM_LOG_MAP_ALLOC      0
#endif

/* Hold a boot report on screen before the game starts: heap, zone, IWAD, and
 * the *measured* frame-tick rate. The point is that it needs no cable and no
 * debug adapter -- the kernel's own log goes to a debug UART, and reading that
 * means clipping a probe to the watch.
 *
 * Also written to uoom.log on the watch's storage, which comes back over USB. */
#ifndef UOOM_BOOT_REPORT
#define UOOM_BOOT_REPORT        1
#endif
#ifndef UOOM_BOOT_REPORT_TICKS
#define UOOM_BOOT_REPORT_TICKS  25      /* ~2.5s at a 10Hz tick */
#endif

/* Where the port writes its own log on the watch. Relative to the app's
 * directory, next to where the IWAD is looked for. */
#ifndef UOOM_LOG_PATH
#define UOOM_LOG_PATH           "uoom.log"
#endif

/* On-screen frame time and WAD read counter. There is no debugger on a wrist,
 * so this is how the port gets tuned. Costs a few hundred cycles a frame. */
#ifndef UOOM_HUD_DIAG
#define UOOM_HUD_DIAG           0
#endif

/* Fire a haptic pulse instead of a gunshot. Requires the service process
 * (haptics are a kernel-owned component, the GUI process cannot reach them). */
#ifndef UOOM_ENABLE_HAPTIC_SFX
#define UOOM_ENABLE_HAPTIC_SFX  1
#endif

/* ---------------------------------------------------------------- filesystem */

/* Where the IWAD is expected on the watch's storage. */
#ifndef UOOM_WAD_DIR
#define UOOM_WAD_DIR            "uoom"
#endif

/* Bytes of RAM given to the lump cache that sits in front of the WAD file.
 * The WAD stays on flash and lumps are pulled on demand; this is the buffer
 * that keeps that from being a per-column disaster. */
#ifndef UOOM_WAD_CACHE_BYTES
#define UOOM_WAD_CACHE_BYTES    (24 * 1024)
#endif

/* Size of a single block in the WAD read cache. eMMC/FATFS likes 4K. */
#ifndef UOOM_WAD_BLOCK_BYTES
#define UOOM_WAD_BLOCK_BYTES    4096
#endif

/* ---------------------------------------------------------------------- zone */

/* DOOM's Z_Malloc arena. Vanilla asks for 6MB and refuses to start below 6MB
 * (i_system.c DEFAULT_RAM/MIN_RAM); patch 0002 replaces I_ZoneBase with a
 * static array of exactly this size.
 *
 * 2048K is the *measured* floor to load and play a level -- below it, level
 * loading dies on a single ~247KB contiguous allocation. Measured against
 * Freedoom Phase 1, whose maps are larger than shareware DOOM's; see
 * docs/03-memory-budget.md for the sweep and for what to try next.
 *
 * Left at the honest number deliberately. It exceeds a stock
 * UNA_APP_GUI_RAM_LENGTH, so the *linker* will refuse the build with a
 * number in hand -- which is far better than the allocator refusing on the
 * watch with a black screen. Lower it only when you have measured that a
 * smaller value survives the WAD you actually ship. */
#ifndef UOOM_ZONE_BYTES
#define UOOM_ZONE_BYTES         (512 * 1024)
#endif

/* Below this there is no point starting. Only used by the heap path below. */
#ifndef UOOM_ZONE_MIN_BYTES
#define UOOM_ZONE_MIN_BYTES     (256 * 1024)
#endif

/* Where the zone comes from.
 *
 *   0 (default) -- a static array in .bss, inside the app's own RAM region,
 *                  bounded by UNA_APP_GUI_RAM_LENGTH.
 *   1           -- malloc, i.e. the kernel's allocator, outside that region.
 *
 * The heap looked like the obviously correct answer on paper: `_sbrk` traps,
 * malloc forwards to the kernel via IAppMemory, and that heap is not counted
 * against RAM_LENGTH -- so it costs nothing that .text and .rodata are also
 * competing for the app region.
 *
 * On hardware it failed at every size from 1MB down to 640KB. The kernel's
 * heap is simply not sized for a game. Meanwhile the app's own region is
 * governed by a number we set ourselves, with no documented cap and no runtime
 * cost for asking. So .bss it is -- see docs/08-first-boot-debugging.md.
 *
 * Kept as a flag rather than deleted, because which one works is a property of
 * the kernel firmware, not of this port. */
#ifndef UOOM_ZONE_FROM_HEAP
#define UOOM_ZONE_FROM_HEAP     0
#endif

/* Take the zone from the *service* process's .bss and have the GUI read it
 * across the process boundary.
 *
 * Measured on hardware: the loader hands out between 878KB and 1009KB per app
 * image, and the GUI already needs ~600KB for DOOM's code and static arrays.
 * That leaves ~300KB for a zone that wants 700KB -- so a single process cannot
 * do it, and the kernel's heap (UOOM_ZONE_FROM_HEAP) could not supply 640KB.
 *
 * The service is a separate image with its own region and its own ceiling, and
 * there is no MMU. So the zone goes there and the GUI asks for the pointer.
 * See UoomMessage::ZoneRequest. */
#ifndef UOOM_ZONE_IN_SERVICE
#define UOOM_ZONE_IN_SERVICE    0
#endif

/* Static array of this many KB, touched once at startup so it cannot be
 * optimised away. Nothing uses it: it exists to find out how large an app
 * image the kernel's loader will actually accept, by adding weight to the
 * 13KB smoke build until it stops running. Set to 0 for real builds. */
#ifndef UOOM_BALLAST_KB
#define UOOM_BALLAST_KB         0
#endif

/* Screen wipes (the "melt" between levels) allocate three extra 64KB screen
 * buffers plus a 64KB transform scratch -- about 192KB of zone peak, for two
 * seconds of nostalgia. Off by default; the zone is the scarcest thing here. */
#ifndef UOOM_ENABLE_WIPES
#define UOOM_ENABLE_WIPES       0
#endif

/* Renderer limits. Upstream sizes these for a 1993 PC with memory to spare;
 * on this budget they are the four biggest static arrays in the binary.
 * Overflowing MAXVISPLANES or MAXDRAWSEGS is a hard I_Error, not a graceful
 * degradation, so these need testing on busy maps before they ship. */
#ifndef UOOM_MAXVISPLANES
/* 96 was measured against *Freedoom* Phase 1, whose maps are four times the
 * geometry of the shareware ones. Against DOOM1.WAD, all three attract demos
 * pass at 48; the published high-water marks for them are 33, 38 and 41. 64
 * keeps 1.5x margin over the worst of those and hands 21KB back to the zone,
 * which is where the failures actually are.
 *
 * Raise it to 96 if you ship Freedoom. Overflow is a hard I_Error -- and only
 * became one after patch 0007, since R_CheckPlane never bounds-checked and
 * silently wrote 664 bytes past the array instead. */
#define UOOM_MAXVISPLANES       64
#endif
#ifndef UOOM_MAXOPENINGS_DIV
#define UOOM_MAXOPENINGS_DIV    4       /* SCREENWIDTH*64 / this -- saves 30.7KB */
#endif
#ifndef UOOM_MAXDRAWSEGS
/* Shareware demo high-water marks are 65-67. GBADoom ships 192. 128 leaves
 * real headroom, and overflow degrades to hall-of-mirrors rather than a
 * crash. Saves 7.7KB against upstream's 256. */
#define UOOM_MAXDRAWSEGS        96
#endif
#ifndef UOOM_MAXVISSPRITES
/* Demo marks are 54-67. Overflow drops sprites rather than crashing, so this
 * fails soft and 80 is a reasonable trade for a kilobyte of zone. */
#define UOOM_MAXVISSPRITES      80
#endif

/* ------------------------------------------------------------------- logging */

#ifndef UOOM_LOG
#define UOOM_LOG                1
#endif

/* Also write diagnostics through the kernel's logger. Off by default: it only
 * reaches a debug UART nobody has a probe on, and the SDK notes the GUI-side
 * logger goes through TouchGFX's HAL, which this port never initialises. The
 * file log is the one that actually gets read. */
#ifndef UOOM_KERNEL_LOG
#define UOOM_KERNEL_LOG         0
#endif

/* Service the kernel's message queue from inside uoom_printf while DOOM is
 * initialising.
 *
 * D_DoomMain does seconds of work -- WAD directory, colormaps, texture tables,
 * sprite lumps -- without ever yielding, and during that time the app answers
 * no EVENT_GUI_TICK and, more importantly, no COMMAND_APP_GUI_RESUME or
 * COMMAND_APP_STOP. The kernel sends those with timeouts and expects
 * responses. DOOM's only regular heartbeat during init is its own logging, so
 * that is where the pump goes: every few lines, one frame wait.
 *
 * Costs about a second of startup at a 10Hz tick, and makes uoom_printf
 * briefly blocking, which is surprising enough to be worth this comment. */
#ifndef UOOM_PUMP_DURING_INIT
#if defined(UOOM_ON_WATCH)
#define UOOM_PUMP_DURING_INIT   1
#else
/* Off on the host: there is no kernel to keep happy, and pumping would spend
 * the harness's frame budget on startup and shift every frame index. */
#define UOOM_PUMP_DURING_INIT   0
#endif
#endif
#ifndef UOOM_PUMP_EVERY_LINES
#define UOOM_PUMP_EVERY_LINES   4
#endif

/* Bring up the platform and nothing else: no WAD, no zone, no DOOM. Draws the
 * boot report and holds it. This is the bisection tool -- if a smoke build
 * shows a screen and a full build does not, the problem is DOOM's memory or
 * its init, not the GUI-task takeover. Set from CMake with -DUOOM_SMOKE=ON,
 * which also unlinks DOOM entirely so the binary is tiny. */
#ifndef UOOM_SMOKE_TEST
#define UOOM_SMOKE_TEST         0
#endif

#endif /* UOOM_CONFIG_H */

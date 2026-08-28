# When the watch shows nothing

Written from the first real on-device run, which showed nothing and left an
empty log. That combination was more informative than it looks, and the method
below is what it taught.

## What an empty-but-created `uoom.log` proves

The file existing at all means a great deal ran:

- `main()` was reached, so the loader accepted the image and `AppStart` ran;
- `SDK::KernelBuilder::make(gIKernel)` succeeded, so the loader patched
  `gIKernel` and the ABI version matched;
- `KernelProviderGUI::CreateInstance()` and `Logger_init()` ran;
- `TouchGFXCommandProcessor::GetInstance()` constructed — **our takeover of the
  GUI task works**;
- `uoom_run()` was entered;
- `kernel().fs.file()` and `IFile::open(true, true)` both worked, so the whole
  filesystem path in the platform adapter is sound.

And the emptiness proved two bugs, both mine:

1. **`uoom_log_flush()` never called `IFile::flush()`.** The bytes sat in the
   filesystem's cache and died with the app. A write that is still cached when
   the process dies is a write that never happened.
2. **Every early failure was invisible.** `writeDisplayFrameBuffer` is a silent
   no-op until a `COMMAND_APP_GUI_RESUME` has been dequeued, and the only thing
   that dequeues it is `waitForFrameTick`. The error screen was being drawn
   before the first tick, so it went nowhere.

Both are fixed. The log now flushes through to storage on every newline, and
the port pumps the queue before anything that draws.

## What the smoke build proved on hardware

It ran. The log:

```
UOOM smoke build: platform only, no DOOM
UOOM first tick received
UOOM tick: 10.0 Hz over 20 ticks
UOOM key w
UOOM key 2
UOOM key s
```

Four things settled, all of them previously open questions:

1. **An app may own the GUI task.** `first tick received` means the kernel sends
   `EVENT_GUI_TICK` to our own `main()`, with `UNA_SDK_SOURCES_GUI` unlinked and
   no TouchGFX screen registered anywhere. This was the risk ranked #1.
2. **The tick is 10.0 Hz**, measured on the device. `SDK::GUI::Config::kFrameRate`
   was right and the "typically 30-60 FPS" in the TouchGFX port document is
   stale prose.
3. **PRESS and RELEASE codes both arrive** — `w`, `2`, `s` is press, click and
   release of SW3, in that order, which also confirms the SW3 = L2 mapping.
   Hold-based movement is therefore possible, and the click-only fallback in
   `uoom_input.c` will correctly disable itself the moment a press is seen.
4. The panel, the log file and the button queue all work through the platform
   adapter as written.

And the full build failed with `zone: could not allocate` — which was the
port's own message, so the fatal path works too, and the answer it gave is that
**the kernel's heap cannot supply 640 KB**, let alone a megabyte.

## Where the zone actually has to live

Moving the zone to `malloc` looked correct on paper: `_sbrk` traps, `malloc`
forwards to the kernel's allocator through `IAppMemory`, and that heap is not
counted against `UNA_APP_GUI_RAM_LENGTH` — so it seemed free of the fact that
`.text` and `.rodata` compete for the app's region.

On hardware it failed at every size from 1 MB down to 640 KB. The kernel's heap
is not sized for a game.

So the zone is a static array in `.bss` again (`UOOM_ZONE_FROM_HEAP=0`), inside
the app's own region — which is governed by a number we set ourselves, with no
documented cap and no runtime cost for asking. The full build is now
**1 647 335 bytes of RAM** with a 1 MB zone, and the `.uapp` is unchanged at
306 KB because `.bss` is zero-fill and never lands in the image.

Which leaves exactly one question: **how large an app will the loader accept?**

## The loader's ceiling, measured

Every probe is the 13 KB smoke app with N KB of static `.bss` touched one byte
per 4 KB page at startup, so a build that runs is proof the loader handed that
much over.

| requested | GUI process | verdict |
|---|---|---|
| 91 KB | smoke, no ballast | **runs** |
| 616 KB | ballast 512K | **runs** |
| 747 KB | ballast 640K | **runs** |
| 878 KB | ballast 768K | **runs** |
| 1 009 KB | ballast 896K | fails |
| 1 140 KB | ballast 1024K | fails |
| 1 664 KB | ballast 1536K | fails |

**The ceiling is between 878 KB and 1 009 KB per app image.** No number in the
SDK predicts this; `UNA_APP_GUI_RAM_LENGTH` has no documented maximum and is
only a link-time bound.

## It renders

DOOM's title screen appeared on the watch. What it took, in the order the
failures came:

1. The log was created and empty — `IFile::write` without `IFile::flush()`.
2. Every early failure was invisible — the panel drops frames until a
   `COMMAND_APP_GUI_RESUME` has been dequeued, and the error screen was being
   drawn before the first tick.
3. `FixedDiv`'s 64-bit divide had no `__aeabi_ldivmod`, because the SDK's
   linker script discards `libgcc.a`.
4. The zone would not fit: the kernel's heap refused 640 KB and the loader
   refuses a single image much over ~900 KB. It lives in the service process,
   with the address passed at run time.
5. **The custom-message queue was never drained.** `waitForFrameTick()` only
   *queues* application-specific messages onto `mUserQueue`; it returns on
   `EVENT_GUI_TICK` without touching them. In a TouchGFX app the generated
   `FrontendApplication::handleTickEvent()` calls
   `callCustomMessageHandler()` — and grep says **nothing in the SDK's own
   `Libs` or `Examples` ever does**. So an app that owns its loop must call it
   itself, and one that forgets watches its own service's messages pile up in a
   ten-deep queue that drops the oldest.

   The service log is what settled it: four `zone grant sent` against a GUI
   that never saw one. Neither side was broken; the queue between them was
   never emptied.

Every one of those five was invisible from the outside and produced the same
symptom. The service's own log file and the per-tick tracing are what made them
separable.

## The loader's two ceilings

Nine measurements, and they need **both** constraints to explain:

| build | GUI | service | combined | verdict |
|---|---|---|---|---|
| smoke | 91 | 7 | 98 | runs |
| ballast 512K | 616 | 7 | 623 | runs |
| ballast 640K | 747 | 7 | 754 | runs |
| ballast 768K | 878 | 7 | 885 | runs |
| ballast 896K | **1 009** | 7 | 1 016 | fails |
| split 512g512s | 616 | 531 | **1 147** | runs |
| z512K | 599 | 532 | 1 131 | **runs — this is the shipping build** |
| z640K | 599 | 663 | **1 262** | fails |
| 768K zone | 599 | 793 | 1 392 | fails |

- **Per image: (878, 1 009] KB.** `ballast896K` failed with a 7 KB service, so
  nothing about the pair explains it.
- **Combined: [1 147, 1 262) KB.** `split512g512s` ran at 1 147 KB, so the
  ceiling is not per-image alone either.

Either constraint alone contradicts one of the rows. Both together fit all
nine. Nothing in the SDK predicts either number.

The shipping build sits at 1 131 KB, so there is somewhere between 16 KB and
131 KB of headroom — and the way to spend it on a bigger zone is to shrink the
GUI, not to ask for more. `docs/03-memory-budget.md` lists where the GUI's
599 KB goes; roughly 130 KB of it is mechanically removable (native 240x240,
one framebuffer instead of two, dropping `statdump` and `m_config`, 16-bit
sine tables).

## A correction: the ceiling may not be per process

The two-process arrangement below was reasoned from data that did not support
it. Every measurement taken so far is equally consistent with a ceiling on the
**whole app** rather than on each image:

| build | GUI | service | combined | verdict |
|---|---|---|---|---|
| smoke | 91 | 7 | 98 | runs |
| ballast 512K | 616 | 7 | 623 | runs |
| ballast 640K | 747 | 7 | 754 | runs |
| ballast 768K | 878 | 7 | 885 | runs |
| ballast 896K | 1 009 | 7 | 1 016 | fails |
| full, 768K zone in the service | 599 | 793 | **1 392** | **fails** |

Every probe until the last put all the weight in the GUI and left the service
at 7 KB, so "per process" and "whole app" predict the same outcome for all of
them. The full build is the first case where they differ — and it failed, which
is what a whole-app ceiling predicts.

`UOOM-svcballast768K` cannot settle it either: 884 KB combined is under the
ceiling on both readings.

**The probe that does settle it** is `UOOM-split512g512s`: 512 KB of ballast in
*each* process, so GUI 616 KB and service 531 KB — each comfortably under the
per-process figure, 1 147 KB combined and well over the whole-app one.

- If it runs, the ceiling is per process and the arrangement below is right.
- If it fails, the ceiling is on the app, the service bought nothing, and
  DOOM's 599 KB of non-zone footprint has to come down instead. That is a
  bigger project: see `docs/reference/prior-art.md`, where the same 240x240
  DOOM was fitted into 256 KB — but by rewriting structures, not by moving
  them.

## Which is why the zone lives in the service process

The arithmetic does not work in one process:

| | |
|---|---|
| GUI non-zone footprint, measured | **599 KB** |
| Loader ceiling | ~880-1 000 KB |
| Zone that would fit | ~300 KB |
| Zone DOOM needs (shareware, ARM) | ~700 KB |

And the kernel's heap could not supply 640 KB either.

But the service is **a separate image with its own region and its own
ceiling**, it needs about 7 KB for itself, and there is no MMU — the SDK's
platform overview states outright that "the app can read entire MCU memory".
So the arena goes there:

```
service process   793 KB   =  7 KB of service + a 768 KB zone
GUI process       599 KB   =  DOOM's code, static arrays, both framebuffers
```

Both are sizes the probes proved loadable. The GUI asks for the address at run
time (`UoomMessage::ZoneRequest` → `ZoneGrant`) because both images are
position-independent and placed by the loader, so nothing can be agreed at link
time. The GUI re-asks every second until answered, since the service may not
have reached its message loop yet.

Two details that matter: the service must **not** exit on
`COMMAND_APP_NOTIF_GUI_STOP` while it is holding the arena, and the array has
to be *referenced* or `--gc-sections` deletes it — which it silently did, the
first time.

This is the arrangement `tools/build-watch.sh` now builds by default.
`-DUOOM_ZONE_IN_SERVICE=OFF` still builds the single-process version, and warns.

## Probing the loader

```sh
tools/probe-loader.sh 512 1024 1536
```

Each build is the 13 KB smoke app with N KB of static `.bss` bolted on and
touched one byte per 4 KB page at startup, so a build that runs is proof the
loader handed over that much. They all have the same `APP_ID`, so each one you
install replaces the last.

| artifact | RAM requested |
|---|---|
| `UOOM-smoke_*.uapp` | 91 KB — confirmed working |
| `UOOM-ballast512K_*.uapp` | 616 KB |
| `UOOM-ballast1024K_*.uapp` | 1 140 KB |
| `UOOM-ballast1536K_*.uapp` | 1 664 KB |
| `UOOM_*.uapp` (full) | 1 647 KB |

**`ballast1536K` is a direct proxy for the full build** — within 17 KB of the
same demand. If it draws its boot report, the full build will load, and any
remaining failure is DOOM's, not the loader's. If it does not, the largest one
that runs tells you the ceiling, and `UOOM_ZONE_BYTES` has to come down to
match:

```sh
# e.g. if 1024K ballast runs but 1536K does not
cmake -DUOOM_ZONE_BYTES=$((512*1024)) ...    # and lower UNA_APP_GUI_RAM_LENGTH
```

A 512 KB zone will not survive a real level — the measured floor for shareware
DOOM is 1024 KB on the host and somewhat less on ARM — but it will boot to the
title screen, which is enough to prove the render path on real glass.

## The bisection build

A device with no console gives you one bit of information per run, so the first
job is to make that bit mean something. Four things could produce a blank
screen — the loader refusing the image, our GUI-task takeover, DOOM's memory,
or DOOM's init — and they look identical from the outside.

```sh
tools/build-watch.sh --smoke clean
```

builds `Output/UOOM-smoke_*.uapp`: **13 KB, 91 KB of RAM, DOOM not linked at
all**. Same `APP_ID` as the full build, so you install one *instead of* the
other into the same `Apps/UOOM/` directory. Smaller than any stock UNA app. It opens the log, pumps the queue,
measures the tick rate and holds the boot report on screen. R2 exits.

| Smoke build | Full build | What it means |
|---|---|---|
| shows a screen | shows a screen | it works; go play |
| shows a screen | blank | the platform is fine. It is DOOM's memory or DOOM's init — read how far `uoom.log` gets |
| blank | blank | the loader or the GUI-task takeover. Not RAM: 91 KB is less than a stock app |

## Reading how far it got

With the flush fixed, `uoom.log` is a trace. The markers, in order:

```
UOOM start                          <- uoom_run() entered, log open
UOOM first tick received            <- the kernel is talking to us; the panel
                                       will now accept frames
UOOM heap: total ..K free ..K ...    <- RequestMemoryInfo answered
UOOM: iwad uoom/DOOM1.WAD           <- the IWAD was found
UOOM: entering doomgeneric_Create
                           Doom Generic 0.1     <- DOOM's own banner starts
Z_Init: Init zone memory allocation daemon.
zone: ...... bytes                  <- the zone was allocated
V_Init: allocate screens.
W_Init: Init WADfiles.
 adding uoom/DOOM1.WAD
...
UOOM: doomgeneric_Create returned   <- DOOM is fully initialised
UOOM tick: ..:. Hz measured over 25 ticks
```

Where it stops is the answer:

- **Stops at `UOOM start`** — the first `waitForFrameTick()` never returned. The
  kernel is not sending us ticks, which means the GUI process is not the one it
  thinks it is talking to.
- **Stops after `first tick` but before `iwad`** — no IWAD where the port looks.
  You should be seeing the instruction screen; if you are not, the panel path
  is broken rather than the search.
- **Stops in DOOM's banner** — an `I_Error` that did not survive to be logged,
  or a hard fault. The last line names the phase.
- **Stops at `zone:` or just after** — the zone allocation. Compare the `HEAP`
  line: if the kernel's largest free block is under `UOOM_ZONE_BYTES`, lower it
  (`-DUOOM_ZONE_BYTES=$((640*1024))`) and try again.
- **Reaches `doomgeneric_Create returned`** — DOOM is up and the problem is in
  rendering or presenting, which is a much smaller search.

## Other things worth trying, in order

```sh
# a smaller zone, if the HEAP line says the kernel cannot spare a megabyte
tools/build-watch.sh clean            # after editing UOOM_ZONE_BYTES, or:
cmake -DUOOM_ZONE_BYTES=$((640*1024)) ...

# turn the kernel logger back on, if you do have a debug adapter
-DUOOM_KERNEL_LOG=1

# frame time and WAD read count on screen during play
-DUOOM_HUD_DIAG=1

# if the boot report is the thing that is failing, skip it
-DUOOM_BOOT_REPORT=0
```

## What is still unverified

The platform layer is proved. What is not:

1. **The loader's app-size ceiling.** The probe above answers it. This is now
   the only thing between the port and a running game.
2. **The 24 KB GUI stack.** DOOM recurses through the BSP tree; a fault during
   `P_SetupLevel` rather than during init would point here.
3. **Init responsiveness.** `D_DoomMain` does seconds of work without yielding,
   so the port pumps the queue from inside its logging
   (`UOOM_PUMP_DURING_INIT`, every fourth line). If the kernel still gives up
   on the app during init, that interval needs to be shorter.
4. **Whether 10 Hz is playable**, and whether the input timings in
   `uoom_input.c` -- tuned around a 50 ms frame -- want retuning for a 100 ms
   one.

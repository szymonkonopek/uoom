# Architecture

## The shape of a UNA app, and where DOOM fits in it

A UNA app is **two processes** packed into one `.uapp`:

- a **Service** ELF -- "*runs as the main application thread*", owns sensors,
  storage and app lifecycle;
- a **GUI** ELF -- TouchGFX, owns the framebuffer, receives the kernel's frame
  tick and the button events.

They talk over the kernel message system. The SDK's examples put all the real
work in the Service and keep the GUI as a thin view.

**UOOM inverts that.** DOOM goes in the **GUI** process, and the Service is a
thin supervisor. Three reasons, in order of weight:

1. **The framebuffer lives there.** `writeDisplayFrameBuffer()` is a method on
   `TouchGFXCommandProcessor`, which is GUI-side. Rendering in the Service
   would mean shipping 57 KB per frame across the IPC boundary, or passing raw
   pointers between processes and hoping the MPU config permits it.
2. **The frame tick lives there.** `waitForFrameTick()` / `EVENT_GUI_TICK` is
   how you get paced at 30-60 Hz without polling. DOOM's main loop wants
   exactly that shape.
3. **The buttons arrive there.** `EVENT_BUTTON` is translated by the GUI-side
   command processor.

The GUI process also has the larger RAM allowance by default (600 K vs 500 K)
and full kernel access via `SDK::KernelProviderGUI::GetInstance().getKernel()`,
which means the filesystem is reachable for the WAD.

What is left for the Service: almost nothing. Haptics turned out to be a plain
kernel message (`RequestVibroPlay`) that the GUI process can send itself, so
the Service is now only there because the kernel expects a service to exist,
and as a home for anything that must outlive a GUI suspend. Its RAM allowance
is set to 64 K so the rest can go where DOOM is.

**UOOM does not link TouchGFX at all.** The SDK's `UNA_SDK_SOURCES_GUI` group
owns `main` *and* the `touchgfx_*` entry points, and does not compile without a
TouchGFX Designer project — so the way to own the render loop is to omit that
group and supply our own `main()`. One file from it is worth borrowing by path:
`TouchGFXCommandProcessor.cpp` includes no TouchGFX headers despite its name and
carries the whole kernel-side protocol (frame tick, STOP/SUSPEND/RESUME, the
button queue, framebuffer submit). See `Software/Libs/Sources/UoomMain.cpp`.

```
              +-------------------- .uapp --------------------+
              |                                              |
   +----------+-----------+          +---------------------+  |
   |  Service ELF (64 K)  |          |   GUI ELF (600 K+)  |  |
   |                      |  custom  |                     |  |
   |  Service.cpp         |<-------->|  UoomGui.cpp        |  |
   |   - haptic pulses    | messages |   - frame loop      |  |
   |   - lifecycle        |          |   - DOOM            |  |
   +----------+-----------+          +----------+----------+  |
              |                                 |             |
              +---------------- kernel ---------+-------------+
                     fs / display / buttons / timers
```

## Layers

```
   third_party/doomgeneric        unmodified upstream DOOM
             |
             |  DG_Init / DG_DrawFrame / DG_GetKey / DG_GetTicksMs / DG_SleepMs
             v
   doomgeneric_uoom.c             the doomgeneric backend: owns the frame loop,
             |                    maps uoom actions -> DOOM keycodes
             |
   +---------+-----------+---------------+--------------+
   |         |           |               |              |
 uoom_video uoom_input  uoom_file      uoom_sys      uoom_present
 (palette,  (4-button   (WAD access,   (time, exit,  (hand the frame
  resample) state m/c)   lump cache)    panic)        to the panel)
   |         |           |               |              |
   |         |           v               v              v
   |         |     uoom_una_file.cpp  ...........  uoom_una_present.cpp
   |         |     (IFileSystem/IFile)             (TouchGFXCommandProcessor)
   |         |
   +---------+--- pure C, no platform, no DOOM: tested on the host
```

The split is deliberate: **everything with logic in it is platform-free and
testable off-device**, and everything platform-specific is a thin adapter with
no decisions in it. `uoom_video` and `uoom_input` between them hold all the
interesting behaviour of this port, and `tests/run.sh` runs both on a laptop in
under a second across four build configurations.

## The frame loop

doomgeneric's contract is `doomgeneric_Create(argc, argv)` once, then
`doomgeneric_Tick()` forever; the backend supplies five callbacks. Ours:

| doomgeneric callback | UOOM implementation |
|---|---|
| `DG_Init` | init video tables, input, open the WAD, allocate nothing else |
| `DG_DrawFrame` | `uoom_video_blit()` then `uoom_present()` |
| `DG_SleepMs` | `kernel.sys.delay(ms)` |
| `DG_GetTicksMs` | `kernel.sys.getTimeMs()` |
| `DG_GetKey` | drain `uoom_input_pop()`, map action -> DOOM keycode |
| `DG_SetWindowTitle` | ignored |

The loop itself is driven by the kernel, not by DOOM:

```
forever:
    waitForFrameTick()                  # kernel paces us, 10 Hz
    while getKeySample(code):           # drain the button queue
        uoom_input_feed_code(code, now)
    uoom_input_tick(now, ctx)           # resolve taps/holds/chords
    doomgeneric_Tick()                  # -> DG_GetKey, render, DG_DrawFrame
```

Pumping that queue is not optional even if you do not want the pacing:
`writeDisplayFrameBuffer` silently drops every frame until a
`COMMAND_APP_GUI_RESUME` has been dequeued, so a loop that renders without
pumping shows a black screen that looks like a driver fault.

`ctx` is `UOOM_CTX_MENU` when DOOM's `menuactive` is set, `UOOM_CTX_GAME`
otherwise -- the one place the port reaches into a DOOM global, and it is worth
it: without it the same four buttons cannot both drive the player and navigate
the menu.

## Lifecycle

The kernel can suspend the GUI (`COMMAND_APP_GUI_SUSPEND`) when the watch face
comes back, and `writeDisplayFrameBuffer()` silently drops frames while
suspended. On suspend the port calls `uoom_input_release_all()` -- otherwise the
player keeps walking into a wall in the background -- and stops ticking DOOM.
On resume it resends the palette and the current frame.

DOOM has no notion of being paused, so a long suspend shows up as a large jump
in `DG_GetTicksMs`. `d_loop.c` clamps that, but the safe thing is to not tick at
all while suspended, which is what the loop above does naturally.

## What is *not* ported

- **Sound and music.** The watch has a PWM buzzer and a DRV2605 haptic motor,
  no audio path. All of `i_sound` / `i_sdlsound` / `i_sdlmusic` is stubbed out.
  Gunfire and pain instead become haptic pulses, via a custom message to the
  Service (`UOOM_ENABLE_HAPTIC_SFX`).
- **Networking / multiplayer.** No.
- **Savegame compatibility with PC DOOM.** Saves work, but they live in the
  app's own directory on the watch.
- **The 320-pixel status bar, in native render mode.** See `docs/04-video.md`.

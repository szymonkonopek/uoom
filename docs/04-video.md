# Video: DOOM's 8-bit output on a 240x240 ABGR2222 panel

## What the hardware gives us

| Property | Value | Source |
|---|---|---|
| Panel | 240 x 240 | UNA TouchGFX Port Architecture: *"supports a 240x240 pixel display with 8-bit color depth using ABGR2222 format"* |
| Framebuffer | `static uint8_t sFrameBuffer[57600]` (240x240x1 byte) | same |
| Pixel format | ABGR2222 -- 2 bits per channel + 2-bit alpha | same |
| Rendering | software only; `STM32DMA` is a **stub** ("not supported") | same |
| Flush | `TouchGFXCommandProcessor::writeDisplayFrameBuffer(const uint8_t*)` sends a `RequestDisplayUpdate` message with a raw buffer pointer, 1s timeout | same |
| Update region | ignored -- `x`, `y`, `width`, `height` are documented as *"unused, always full screen"* | same |
| Frame pacing | kernel `EVENT_GUI_TICK`, *"typically 30-60 FPS"*, awaited via `waitForFrameTick()` | same |
| LCD driver class | `LS012B7DD06A` | UNA Architecture Deep Dive |

And what the panel datasheet adds (Sharp SPEC LCP-2619063C), which the SDK
docs never mention:

| Property | Value |
|---|---|
| Technology | Sharp **Memory-in-Pixel**, reflective, 6-bit parallel (not SPI) |
| **Active area** | a **circle** of diameter 30.24 mm -- radius **120 px** -- inscribed in the 240x240 grid |
| Native colours | **64** -- 2 bits per subpixel, by spatial area gradation (MSB block = 2/3 of the subpixel area, LSB = 1/3) |
| Frame rate | **30 Hz typical, 33 Hz maximum** |
| Frame data time | ~25 ms of every 33 ms frame |

Three consequences dominate every decision below.

**1. The framebuffer is 8 bits per pixel and indexed-adjacent.** This is an
enormous stroke of luck. DOOM is a *palettised* renderer: everything it draws
ends up as one byte per pixel in `I_VideoBuffer`, and the palette is applied at
the very last moment. doomgeneric's stock backends throw that away -- they
expand every frame into a 32-bit `DG_ScreenBuffer` (320x200x4 = **256 KB**)
because SDL and X11 want true colour. On this watch we do not need that buffer
at all. We go 8bpp -> 8bpp through a lookup table and save a quarter of a
megabyte, which on a 600 KB budget is the difference between shipping and not.

**2. The panel's 64 colours and ABGR2222's 64 colours are the same 64
colours.** This was the port's biggest open question and it resolved in our
favour. The panel gets four reflectance levels per channel by splitting each
subpixel into a 2/3-area block and a 1/3-area block; ABGR2222 gives four levels
per channel. They line up exactly, so nothing is quantised twice and the
palette table we build *is* the panel's two bit-planes. Confirmed layout, from
TouchGFX's own `LCD8bpp_ABGR2222.hpp`:

```
bit  7 6   5 4   3 2   1 0
     A A   B B   G G   R R          A=3 (0xC0) is opaque
```

```cpp
// TouchGFX 4.20, verbatim -- note it *truncates*, taking the top two bits
static uint8_t getNativeColorFromRGB(uint8_t red, uint8_t green, uint8_t blue)
{
    return 0xC0 | ((blue & 0xC0) >> 2) | ((green & 0xC0) >> 4) | ((red & 0xC0) >> 6);
}
// and expands back by multiplying the 2-bit level by 0x55: 0, 85, 170, 255
```

Red is in the **low** bits despite the name reading A-B-G-R left to right.
`uoom_config.h` has this as four shift constants, and `tests/run.sh` asserts
that no channel bleeds into another. UOOM rounds and dithers where TouchGFX
truncates.

**3. Two bits per channel is still only 64 colours.** That is the real cost of
this port.
DOOM's palette is not 256 arbitrary colours; it is a set of 32-step *light
ramps*. Corridors read as corridors because the same texture fades smoothly
into darkness. Snapping each channel to one of four levels turns every one of
those ramps into three or four hard bands, and the effect is much worse than
"slightly wrong colours" -- it destroys depth cues.

## The palette path

`uoom_video_set_palette()` collapses DOOM's 768-byte PLAYPAL entry into
lookup tables of ABGR2222 bytes. Not one table -- **four**, each biased by a
different cell of a 2x2 ordered-dither matrix:

```
phase = (y & 1) * 2 + (x & 1)      ->  palLut[phase][index]
```

A gradient then alternates between the two nearest representable levels in a
fixed checkerboard, which at this pixel pitch reads as an intermediate shade.
Cost: 1 KB of table instead of 256 bytes, and one extra pointer in the blit
loop -- the inner loop is unrolled by two so the phase is a choice of pointer,
not a per-pixel branch.

DOOM calls `I_SetPalette` on every damage flash, every item pickup and every
radiation-suit tint, so rebuilding all four tables has to be cheap: it is
1024 iterations of three integer divides, roughly 20k cycles. At 160 MHz that
is 0.12 ms, and it does not happen on a normal frame.

Set `UOOM_DITHER=0` to get plain round-to-nearest if the dither pattern turns
out to shimmer badly in motion on the real panel. That is a judgement call that
needs eyes on hardware.

## The circle

The panel is addressed as a 240x240 square but only lights a circle of radius
120 inside it. The datasheet's per-line tables make it concrete -- row 240 is
"109 dummy / 22 active / 109 dummy" -- and the arithmetic is unforgiving: at
row 230 only about 96 of the 240 columns are visible.

For the 3D view this barely matters. DOOM's frame is centre-weighted, the
player's view is in the middle, and clipped corners read as vignetting.

**For the status bar it matters a great deal.** In FILL mode the bar lands in
the bottom ~38 rows, exactly where the circle narrows, so the ammo count on the
far left and the arms panel on the far right fall outside the glass. That is not
a cosmetic loss; it is the two numbers a player reads most.

Hence a third scale mode, and an open design question
(`docs/07-open-questions.md`): the proper fix for a round watch is not to
squeeze DOOM's 320-pixel status bar into a circle at all, but to drop it and
draw a compact health/ammo readout inside the safe area. That is a bigger change
than a scale factor, and it wants a play test first.

## The resample

DOOM renders 320x200. The panel is 240x240. Three modes, `UOOM_SCALE_MODE`:

- **FILL** (default) -- stretch to the full 240x240. Worth being precise about
  why this is not as wrong as it looks: DOOM's 320x200 was displayed on a 4:3
  CRT, so its pixels were *tall* (1:1.2). Scaling 200 rows up to 240 is
  therefore the historically **correct** vertical geometry. The horizontal
  320 -> 240 squeeze is the part that is wrong, and it makes the world look
  slightly narrow. On a 1.4-inch screen, using every pixel wins.
- **FIT** -- 240x150 centred, 45 blank rows top and bottom. Geometrically
  faithful, and the blank rows land where the circle was going to clip anyway.
  Costs 37% of an already tiny display.
- **INSCRIBED** -- a 169x169 centred square, the largest that fits inside the
  visible circle (side = r x sqrt(2)). Nothing is ever clipped, the whole
  status bar is readable, and it is the mode to use for screenshots that must
  match what a wearer actually sees.

Implementation is nearest-neighbour through precomputed `xMap[240]` /
`yMap[240]` index tables. Bilinear was rejected: you cannot interpolate
palette *indices*, so it would mean interpolating in RGB and re-quantising to
2 bits per channel -- four lookups and a blend per pixel to fight banding that
the dither already fights for free.

Cost per frame: 57 600 iterations of two dependent loads and a store.
Order of a few hundred thousand cycles, i.e. ~2 ms at 160 MHz. Negligible next
to DOOM's own renderer -- and next to the panel, which needs ~25 ms per frame
of data time and caps the whole system at **30 fps** no matter what the MCU
does. Vanilla DOOM's own logic runs at 35 tics/s, so the panel is the binding
constraint, not the game.

## Alternative: render natively at 240x240

`UOOM_RENDER_MODE=UOOM_RENDER_NATIVE` sets `DOOMGENERIC_RESX/RESY` to 240x240
and drops the resample entirely -- the blit becomes a straight LUT pass, and
every width-dependent renderer array (visplanes, `openings`, `ylookup`,
`columnofs`, the wipe buffers) shrinks by 25%.

The catch: DOOM's status bar is a 320-pixel-wide graphic, and a good deal of
menu and intermission art assumes a 320-pixel canvas. At 240 wide they get
clipped on the right. Whether that is acceptable is a taste question; it is a
one-flag change, so both are kept alive and both are covered by the tests.

## Where the frame goes

Two possible sinks, selected at build time (see `docs/02-architecture.md`):

1. **Through TouchGFX** -- a single full-screen custom widget whose `draw()`
   blits into TouchGFX's framebuffer. Uses only documented API, costs one extra
   57 KB `memcpy` per frame (~0.3 ms) and whatever RAM TouchGFX itself holds.
2. **Direct** -- our own loop: `waitForFrameTick()`, run a DOOM tic, then
   `writeDisplayFrameBuffer(ourBuffer)`. `RequestDisplayUpdate` takes a raw
   pointer, so there is no copy at all and TouchGFX's own 57 KB static
   framebuffer and widget machinery are never linked in.

Option 2 is strictly better on both RAM and time; option 1 is the one the docs
promise will work. The port abstracts the sink behind `uoom_present()` so this
stays a build-time choice rather than a rewrite. Deciding it needs the SDK
sources in hand -- see `docs/07-open-questions.md`.

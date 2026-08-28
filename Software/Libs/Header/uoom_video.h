/* uoom_video.h -- DOOM's 8-bit indexed output -> the watch's ABGR2222 panel
 *
 * Pure C, no platform dependencies: the same code runs in the host harness
 * (host/) and on the watch. The platform only has to hand us a 240x240 byte
 * buffer and push it to the display when we say so.
 */
#ifndef UOOM_VIDEO_H
#define UOOM_VIDEO_H

#include <stdint.h>
#include "uoom_config.h"

/* Build the palette lookup tables. `pal768` is DOOM's PLAYPAL entry:
 * 256 RGB triplets, 8 bits per channel. Called from I_SetPalette, which DOOM
 * hits on every damage flash and item pickup, so this must stay cheap
 * (256 entries * 4 dither phases = 1KB of table, ~3k cycles to rebuild). */
void uoom_video_set_palette(const uint8_t *pal768);

/* Recompute the resample tables. Called once from uoom_video_init(). */
void uoom_video_init(void);

/* Convert one DOOM frame into panel pixels.
 *   src -- UOOM_DOOM_W * UOOM_DOOM_H bytes, palette indices (I_VideoBuffer)
 *   dst -- UOOM_PANEL_BYTES bytes, ABGR2222
 * Both may be the same size (native mode) or not (scaled mode). */
void uoom_video_blit(const uint8_t *src, uint8_t *dst);

/* The panel buffer the port owns: UOOM_PANEL_BYTES of ABGR2222, handed to
 * uoom_plat_present() when a frame is finished. Lives here rather than in the
 * doomgeneric backend so the error and boot-report screens -- and a build with
 * no DOOM in it at all -- can still draw. */
uint8_t *uoom_present_buffer(void);

/* Pack a 24-bit colour into one ABGR2222 pixel, opaque. Exposed for tests and
 * for drawing the port's own UI (error screens, the "insert WAD" message). */
uint8_t uoom_video_pack_rgb(uint8_t r, uint8_t g, uint8_t b);

/* Expand an ABGR2222 pixel back to 24-bit RGB. Only used by the host harness
 * to write PNG/PPM screenshots, and by tests. */
void uoom_video_unpack_rgb(uint8_t px, uint8_t *r, uint8_t *g, uint8_t *b);

#endif /* UOOM_VIDEO_H */

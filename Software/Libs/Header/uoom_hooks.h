/* uoom_hooks.h -- the handful of symbols the patched DOOM sources call
 *
 * Kept in its own header so the patches in tools/patches/ stay one-line
 * includes rather than dragging port declarations into engine files.
 */
#ifndef UOOM_HOOKS_H
#define UOOM_HOOKS_H

#include <stdint.h>

#include "uoom_config.h"

/* Called from the patched I_SetPalette with a gamma-corrected 768-byte
 * palette. Builds the ABGR2222 lookup tables. */
void UOOM_SetPalette(const uint8_t *pal768);

/* Called from the patched I_FinishUpdate with DOOM's 8-bit indexed screen.
 * Resamples, palettises and pushes the frame to the panel. */
void UOOM_FinishUpdate(const uint8_t *videoBuffer);

/* Replaces I_ZoneBase's malloc(6MB). */
unsigned char *uoom_zone_base(int *size);

/* Replaces I_InitGraphics's Z_Malloc of I_VideoBuffer. */
unsigned char *uoom_screen_buffer(void);

/* Replaces I_Error's fprintf(stderr) + exit(). Does not return. */
void uoom_fatal(const char *fmt, ...);

#endif /* UOOM_HOOKS_H */

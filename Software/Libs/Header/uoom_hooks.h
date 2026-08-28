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

/* What DOOM's I_Quit becomes. Upstream's exit(0) sits inside #if ORIGCODE,
 * which is undefined here, so I_Quit ran its atexit handlers and returned --
 * "Quit Game" did nothing at all. */
void UOOM_Quit(void);

/* Release the finished level's zone allocations.
 *
 * Vanilla holds a level's PU_LEVEL data until the *next* P_SetupLevel, which
 * means the intermission and the title screen ask for a 68KB full-screen
 * graphic while ~250KB of dead level geometry still fragments the arena. On a
 * PC that is free real estate; here it is the difference between finishing a
 * level and Z_Malloc failing on 68208 bytes. Called from the two places a
 * level stops being needed. */
void UOOM_ReleaseLevel(void);

#endif /* UOOM_HOOKS_H */

/* uoom_sys.h -- the odds and ends DOOM expects from a hosted C environment
 *
 * DOOM assumes a 1993 Unix box: a printf that goes somewhere, a multi-megabyte
 * malloc, an exit(), and BSD's strcasecmp. None of those are free on a watch.
 */
#ifndef UOOM_SYS_H
#define UOOM_SYS_H

#include <stdint.h>
#include <stddef.h>

#include "uoom_config.h"

/* The zone. Replaces I_ZoneBase's `malloc(6 * 1024 * 1024)` with a static
 * array of exactly UOOM_ZONE_BYTES -- see tools/patches/0002.
 * Static, not malloc'd, so the linker refuses the build rather than the
 * allocator refusing at boot in front of the user. */
unsigned char *uoom_zone_base(int *size);

/* DOOM's 8-bit indexed screen (I_VideoBuffer). Static rather than the 64KB
 * PU_STATIC block upstream takes out of the zone at startup: same total RAM,
 * but it stops that block from fragmenting the arena, which measurably lowers
 * the zone floor. */
unsigned char *uoom_screen_buffer(void);

/* Filled in by uoom_zone_base(); shown on the boot report screen. */
extern uint32_t uoom_zone_got;
extern uint32_t uoom_heap_largest;

/* DOOM's printf, routed to the kernel logger. Applied to the engine via
 * -Dprintf=uoom_printf, which is cheaper than patching 95 call sites. */
int uoom_printf(const char *fmt, ...);

/* Stop servicing the kernel queue from inside uoom_printf. Called once DOOM's
 * init is done and the frame loop takes over that job. */
void uoom_sys_end_init(void);

/* What DOOM's I_Error becomes. Shows the message on the panel and stops. */
void uoom_fatal(const char *fmt, ...);

/* BSD string compares. newlib-nano does not reliably provide these, and
 * W_CheckNumForName is in DOOM's hot path. */
int uoom_strcasecmp(const char *a, const char *b);
int uoom_strncasecmp(const char *a, const char *b, size_t n);

#endif /* UOOM_SYS_H */

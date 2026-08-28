/* uoom_sys.c -- see uoom_sys.h */

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "uoom_sys.h"
#include "uoom_plat.h"
#include "uoom_text.h"
#include "uoom_file.h"

/* ---------------------------------------------------------------- the zone
 *
 * malloc, not a static array -- and on this platform that is not the lazy
 * choice, it is the correct one:
 *
 *   - The app's linker script has a single MEMORY region and no flash region.
 *     .text, .rodata, .data, .bss and .stack all live in it, bounded by
 *     UNA_APP_GUI_RAM_LENGTH. A two-megabyte .bss array would have to fit
 *     inside that ceiling alongside DOOM's code.
 *   - malloc does not. `_sbrk` is a hard trap in the SDK's system.cpp; malloc
 *     forwards to the kernel's own allocator through IAppMemory, and that heap
 *     lives *outside* the app's region entirely.
 *
 * So the biggest single object in the port belongs on the kernel heap. It also
 * lets the size be negotiated at run time rather than fixed at link time,
 * which matters because the kernel's heap size is not documented anywhere.
 *
 * Note: do NOT put this in a custom linker section. The .uapp packer copies
 * exactly nine known section names and merely *warns* about anything else, so
 * a custom section builds clean and arrives as garbage on the watch.
 */

uint32_t uoom_zone_got;          /* what the allocator actually gave us */
uint32_t uoom_heap_largest;      /* what the platform said was available */

#if UOOM_ZONE_IN_SERVICE
/* Nothing here: the arena lives in the service process. */
#elif !UOOM_ZONE_FROM_HEAP
/* In .bss, inside the app's own RAM region. Costs the space unconditionally,
 * which is the point: if it does not fit, the *linker* refuses the build with a
 * number, rather than the allocator refusing on the watch with a message the
 * user has to read off a 1.4-inch screen.
 *
 * No custom linker section: the .uapp packer copies nine known section names
 * and merely warns about anything else, so a custom section would build clean
 * and arrive as garbage. */
static unsigned char sZone[UOOM_ZONE_BYTES] __attribute__((aligned(8)));
#endif

unsigned char *uoom_zone_base(int *size)
{
    uint32_t largest = 0u;

    /* Reported before anything is allocated, so a failure is diagnosable from
     * the first boot. Worth doing even on the .bss path: it is the only way to
     * see what the kernel's heap could have offered. */
    uoom_plat_report_memory(&largest);
    uoom_heap_largest = largest;

#if UOOM_ZONE_IN_SERVICE
    {
        uint32_t addr = 0u;
        uint32_t got = 0u;

        if (!uoom_plat_zone_from_service(&addr, &got) || addr == 0u) {
            uoom_fatal("zone: the service did not hand over an arena");
        }
        uoom_printf("zone: %u KB at %p, from the service process\n",
                    (unsigned)(got / 1024), (void *)(uintptr_t)addr);
        uoom_zone_got = got;
        if (size != NULL) {
            *size = (int)got;
        }
        return (unsigned char *)(uintptr_t)addr;
    }
#elif UOOM_ZONE_FROM_HEAP
    {
        size_t want = UOOM_ZONE_BYTES;
        unsigned char *p = NULL;

        if (largest != 0u && (size_t)largest < want) {
            want = (size_t)largest;
        }
        for (;;) {
            p = (unsigned char *)malloc(want);
            if (p != NULL) {
                break;
            }
            if (want <= (size_t)UOOM_ZONE_MIN_BYTES) {
                uoom_fatal("zone: heap gave nothing down to %u KB",
                           (unsigned)(UOOM_ZONE_MIN_BYTES / 1024));
            }
            want -= 64u * 1024u;
        }
        if (want < (size_t)UOOM_ZONE_BYTES) {
            uoom_printf("zone: wanted %u KB, heap gave %u KB\n",
                        (unsigned)(UOOM_ZONE_BYTES / 1024),
                        (unsigned)(want / 1024));
        }
        uoom_zone_got = (uint32_t)want;
        if (size != NULL) {
            *size = (int)want;
        }
        return p;
    }
#else
    uoom_zone_got = (uint32_t)sizeof(sZone);
    uoom_printf("zone: %u KB static\n", (unsigned)(sizeof(sZone) / 1024));
    if (size != NULL) {
        *size = (int)sizeof(sZone);
    }
    return sZone;
#endif
}

/* --------------------------------------------------------- the screen buffer */

static unsigned char sScreen[UOOM_DOOM_W * UOOM_DOOM_H] __attribute__((aligned(4)));

unsigned char *uoom_screen_buffer(void)
{
    return sScreen;
}

/* --------------------------------------------------------------- diagnostics */

#if UOOM_PUMP_DURING_INIT
static uint8_t sPumpOnLog = 1;
#endif

void uoom_sys_end_init(void)
{
#if UOOM_PUMP_DURING_INIT
    sPumpOnLog = 0;
#endif
}

int uoom_printf(const char *fmt, ...)
{
    /* DOOM's longest startup line is the WAD banner; 256 is comfortable and
     * this is not a hot path. Truncation is preferable to a stack overflow. */
    char    line[256];
    va_list ap;
    int     n;

    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

#if UOOM_LOG
    /* File first, kernel logger second, deliberately. The SDK notes that the
     * GUI-side logger writes through TouchGFX's HAL -- which UOOM never
     * initialises -- so if it is going to fault, it must not take the line
     * with it. */
    uoom_log_puts(line);
#if UOOM_KERNEL_LOG
    uoom_plat_log(line);
#endif
#endif

#if UOOM_PUMP_DURING_INIT
    /* See UOOM_PUMP_DURING_INIT: DOOM's init never yields, so its logging is
     * the only place a heartbeat fits. */
    if (sPumpOnLog) {
        static unsigned sLines;

        if ((++sLines % (unsigned)UOOM_PUMP_EVERY_LINES) == 0u) {
            uoom_plat_frame_wait();
            uoom_plat_keep_awake();
        }
    }
#endif
    return n;
}

void uoom_fatal(const char *fmt, ...)
{
    char    line[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    uoom_log_puts("FATAL: ");
    uoom_log_puts(line);
    uoom_log_puts("\n");
    uoom_log_flush();
#if UOOM_KERNEL_LOG
    uoom_plat_log(line);
#endif

    /* The panel will not accept a frame until the kernel has been told we are
     * resumed, so make sure a tick has gone through before drawing the reason
     * we are dying. Bounded, in case the kernel is the thing that is wrong. */
    {
        int i;

        for (i = 0; i < 3; ++i) {
            uoom_plat_frame_wait();
        }
    }
    uoom_text_error_screen(line);
    uoom_plat_panic(line);

    /* uoom_plat_panic must not return; belt and braces. */
    for (;;) {
    }
}

/* -------------------------------------------------------------- BSD strings */

static inline int lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c;
}

int uoom_strcasecmp(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        int d = lower((unsigned char)*a) - lower((unsigned char)*b);

        if (d != 0) {
            return d;
        }
        ++a;
        ++b;
    }
    return lower((unsigned char)*a) - lower((unsigned char)*b);
}

int uoom_strncasecmp(const char *a, const char *b, size_t n)
{
    while (n-- > 0) {
        int d = lower((unsigned char)*a) - lower((unsigned char)*b);

        if (d != 0) {
            return d;
        }
        if (*a == '\0') {
            return 0;
        }
        ++a;
        ++b;
    }
    return 0;
}

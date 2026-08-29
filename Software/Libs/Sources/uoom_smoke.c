/* uoom_smoke.c -- bring up the platform, and nothing else
 *
 * An alternative `uoom_run()` for builds configured with -DUOOM_SMOKE=ON: no
 * WAD, no zone, no DOOM linked at all. It opens the log, pumps the kernel's
 * message queue, measures the frame tick and holds the boot report on screen.
 *
 * The point is bisection. A full build that shows nothing could be failing in
 * any of four places -- the loader refusing the image, our takeover of the GUI
 * task, DOOM's memory, or DOOM's init -- and on a device with no console those
 * look identical. This build removes the last two from the picture: if it draws
 * a screen, the platform layer works and the fault is in DOOM.
 *
 * It is also the smallest useful UNA app this port can produce, which makes it
 * the thing to try first if the loader is the suspect.
 */

#include "uoom_config.h"
#include "uoom_plat.h"
#include "uoom_video.h"
#include "uoom_input.h"
#include "uoom_text.h"
#include "uoom_sys.h"
#include "uoom_file.h"

void uoom_run(void);

#if UOOM_BALLAST_KB > 0
/* Weight, to find the loader's ceiling. Touched below so .bss is genuinely
 * committed rather than optimised away. */
static volatile unsigned char sBallast[(size_t)UOOM_BALLAST_KB * 1024u];
#endif

/* --- how far does the app's filesystem reach? -----------------------------
 *
 * The SDK documents SDK::Kernel::fs as sandbox-rooted -- "/" is the app's own
 * directory, physically 2:/Apps/<AppDir>/ -- which would mean one app cannot
 * read another's files. That matters for any plan to ship the WAD in a second
 * package, so it is worth knowing rather than believing: the docs were wrong
 * once already in this port, about whether waitForFrameTick dispatches app
 * messages.
 *
 * Each path below is opened for reading and, if that fails, listed as a
 * directory. Anything that answers is a way out of the sandbox. */
static void probe_filesystem(void)
{
    static const char *const kPaths[] = {
        "DOOM1.WAD",                /* the baseline: this one must work */

        /* Round two. The first probe established that the installed package
         * survives in the app's own directory, and that ".." lists a real
         * directory outside it -- the volume root, holding the kernel image.
         * Two things it did not establish, and both decide a design:
         *
         * 1. Can the app open its own package? If so, a WAD appended to the
         *    .uapp is readable at no RAM cost. The container tolerates it:
         *    the loader sizes its sections from the headers, and the outer
         *    CRC is the last four bytes, so a payload before it verifies.
         *
         * 2. Is "..' real traversal, or does every path containing it
         *    collapse to one directory? Last time "..", "../", "/.." and
         *    "../UOOM-Assets/DOOM1.WAD" all returned the *same* four-entry
         *    listing, which is what a collapse looks like. "../gps" settles
         *    it: its own contents mean traversal, the root listing again
         *    means collapse. */

        "UOOM-smoke_0.0.0-dev.uapp",        /* our own package, by name */
        "/UOOM-smoke_0.0.0-dev.uapp",

        "../gps",                           /* traversal, or collapse? */
        "../logo_222.bmp",                  /* a file that is really there */
        "../UnaWatch-Kernel_1.0.2.gld",
        "/gps",
        "../..",
        "../nonexistent-xyz",               /* the control: should fail */

        "0:/",
        "1:/",
        "3:/",
    };

    unsigned i;

    uoom_printf("probe: this app's own directory --\n");
    if (uoom_plat_list_dir("/") < 0) {
        uoom_printf("probe: cannot even list \"/\"\n");
    }

    for (i = 0; i < sizeof(kPaths) / sizeof(kPaths[0]); ++i) {
        const char       *path = kPaths[i];
        uoom_plat_file_t *f    = uoom_plat_open(path, 0);
        int               n;

        if (f != NULL) {
            uoom_printf("probe: FILE  %-34s %u bytes\n", path,
                        (unsigned)uoom_plat_filesize(path));
            uoom_plat_close(f);
            continue;
        }

        n = uoom_plat_list_dir(path);
        if (n >= 0) {
            uoom_printf("probe: DIR   %-34s %d entries\n", path, n);
        } else {
            uoom_printf("probe: -     %-34s\n", path);
        }
    }
}

void uoom_run(void)
{
    uint32_t t0;
    uint32_t heapLargest = 0u;
    uint32_t ticks = 0;
    uint32_t hz10 = 0;

    uoom_log_open(UOOM_LOG_PATH);
    uoom_printf("UOOM smoke build: platform only, no DOOM\n");

    probe_filesystem();

    uoom_sys_end_init();        /* this build has its own loop from the start */
    uoom_video_init();
    uoom_input_init();

#if UOOM_BALLAST_KB > 0
    {
        size_t i;

        /* One byte per 4K page: enough that the linker cannot discard it and
         * the loader cannot have given us less than it claimed. */
        for (i = 0; i < sizeof(sBallast); i += 4096u) {
            sBallast[i] = (unsigned char)(i >> 12);
        }
        uoom_printf("UOOM ballast: %u KB committed\n",
                    (unsigned)(sizeof(sBallast) / 1024u));
    }
#endif

    /* The number the SDK documents nowhere, and the reason the full build
     * could not allocate a zone from the heap. */
    uoom_plat_report_memory(&heapLargest);
    uoom_heap_largest = heapLargest;

    /* Pump before drawing anything. writeDisplayFrameBuffer is a silent no-op
     * until a COMMAND_APP_GUI_RESUME has been dequeued, and the only thing
     * that dequeues it is waitForFrameTick -- so a screen drawn before the
     * first tick goes nowhere. */
    uoom_plat_frame_wait();
    uoom_printf("UOOM first tick received\n");
    uoom_plat_keep_awake();

    t0 = uoom_plat_ticks_ms();

    while (!uoom_plat_should_quit()) {
        uint8_t code;
        uint32_t dt;

        uoom_plat_frame_wait();
        ++ticks;

        dt = uoom_plat_ticks_ms() - t0;
        if (dt > 300u) {
            hz10 = (ticks * 10000u) / dt;
        }

        /* Drain the buttons so a press is visibly acknowledged -- any button
         * exits, which is the only control this build has. */
        while (uoom_plat_poll_key(&code)) {
            uoom_printf("UOOM key %c\n", (char)code);
            if (code == 'f' || code == '4') {   /* R2 release / R2 click */
                uoom_printf("UOOM smoke exit on R2\n");
                uoom_log_close();
                return;
            }
        }

        uoom_text_boot_report("SMOKE BUILD", uoom_heap_largest,
                              (uint32_t)UOOM_BALLAST_KB * 1024u, hz10);
        uoom_plat_keep_awake();

        if (ticks == 20u) {
            uoom_printf("UOOM tick: %u.%u Hz over %u ticks\n",
                        (unsigned)(hz10 / 10u), (unsigned)(hz10 % 10u),
                        (unsigned)ticks);
        }
    }

    uoom_printf("UOOM smoke exit\n");
    uoom_log_close();
}

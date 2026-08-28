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

void uoom_run(void)
{
    uint32_t t0;
    uint32_t heapLargest = 0u;
    uint32_t ticks = 0;
    uint32_t hz10 = 0;

    uoom_log_open(UOOM_LOG_PATH);
    uoom_printf("UOOM smoke build: platform only, no DOOM\n");

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

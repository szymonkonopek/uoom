/* Service.cpp -- see Service.hpp */

#include <cstdio>

#include "Service.hpp"

#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/UnaLogger/Logger.h"

#include "UoomMessages.hpp"
#include "uoom_config.h"

#if UOOM_ZONE_IN_SERVICE
/* DOOM's zone, in this process's .bss.
 *
 * The GUI cannot hold it: the loader gives out under a megabyte per image and
 * the GUI needs most of that for DOOM itself. This process needs almost no RAM
 * of its own, so it holds the arena and hands over the address. There is no
 * MMU -- the platform overview says so outright -- and both images are
 * position-independent, which is why the address is asked for at run time.
 *
 * .bss, so the loader zeroes it before we ever run and Z_Init does the rest. */
static unsigned char sZone[UOOM_ZONE_BYTES] __attribute__((aligned(8)));
#endif

#if UOOM_ZONE_IN_SERVICE || UOOM_SVC_BALLAST_KB > 0
/* The service's own log. The GUI writes uoom.log; this process cannot share
 * that handle, and the kernel's logger only reaches a debug UART -- so when
 * the two processes fail to talk to each other, this file is the only way to
 * see which one is not talking.
 *
 * Opened and closed per line: there are a handful of them, and a handle held
 * open would be lost on a crash anyway. */
static void svcLog(SDK::Kernel &kernel, const char *line)
{
    static bool sTruncated;

    auto f = kernel.fs.file("uoom-svc.log");
    if (!f) {
        return;
    }
    /* First line truncates, the rest append. */
    if (!f->open(true, !sTruncated)) {
        return;
    }
    sTruncated = true;
    if (!f->seek(f->size())) {
        /* a fresh file, or seek refused: write from wherever we are */
    }
    {
        size_t wrote = 0;
        size_t n = 0;

        while (line[n] != '\0') {
            ++n;
        }
        f->write(line, n, wrote);
    }
    f->flush();
    f->close();
}
#endif

#ifndef UOOM_SVC_BALLAST_KB
#define UOOM_SVC_BALLAST_KB 0
#endif

#if UOOM_SVC_BALLAST_KB > 0
/* Weight in the *service* process, to find out whether the loader's app-size
 * ceiling is per process or shared. If it is per process, the zone can live
 * here -- there is no MMU and the two regions share one address space, which
 * the SDK's own platform overview states outright -- and the GUI can simply be
 * handed the pointer.
 *
 * Touched one byte per 4K page below, so the linker cannot discard it and the
 * loader cannot have handed over less than it claimed. */
static volatile unsigned char sSvcBallast[(unsigned)UOOM_SVC_BALLAST_KB * 1024u];
#endif

Service::Service(SDK::Kernel &kernel)
    : mKernel(kernel)
    , mGuiStarted(false)
{
}

#if UOOM_ZONE_IN_SERVICE
void Service::grantZone()
{
    const bool ok = SDK::send_msg<UoomMessage::ZoneGrant>(
        mKernel,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(sZone)),
        static_cast<uint32_t>(sizeof(sZone)));

    svcLog(mKernel, ok ? "service: zone grant sent\n"
                       : "service: zone grant FAILED to send\n");
}
#endif

void Service::run()
{
#if UOOM_SVC_BALLAST_KB > 0
    {
        unsigned i;

        for (i = 0; i < sizeof(sSvcBallast); i += 4096u) {
            sSvcBallast[i] = (unsigned char)(i >> 12);
        }

        /* The service's own marker. The GUI's boot report appearing proves the
         * GUI loaded; this proves the *service* did, with that much .bss,
         * which is the thing being measured. The kernel's logger only reaches
         * a debug UART, so a file is the only readable channel. */
        char msg[96];
        int n = snprintf(msg, sizeof(msg),
                         "service: ran with %u KB of ballast committed\n",
                         (unsigned)(sizeof(sSvcBallast) / 1024u));
        if (n > 0) {
            svcLog(mKernel, msg);
        }
    }
#endif

#if UOOM_ZONE_IN_SERVICE
    svcLog(mKernel, "service: entering message loop, zone ready\n");
#endif

    /* Blocking receive with no timeout: this service has no periodic work, so
     * it should never be scheduled except when something arrives. */
    SDK::MessageBase *msg = nullptr;

    while (mKernel.comm.getMessage(msg)) {
        switch (msg->getType()) {

        case SDK::MessageType::COMMAND_APP_STOP:
            mKernel.comm.releaseMessage(msg);
            return;

        case SDK::MessageType::COMMAND_APP_NOTIF_GUI_RUN:
            mGuiStarted = true;
#if UOOM_ZONE_IN_SERVICE
            /* Offer the arena unprompted, as soon as we know the GUI exists.
             * The GUI also asks, but if GUI->service routing is the half that
             * does not work, this direction still gets the job done. */
            svcLog(mKernel, "service: GUI is up, offering the zone\n");
            grantZone();
#endif
            break;

#if UOOM_ZONE_IN_SERVICE
        case UoomMessage::ZONE_REQUEST:
            svcLog(mKernel, "service: zone requested\n");
            grantZone();
            break;
#endif

        case SDK::MessageType::COMMAND_APP_NOTIF_GUI_STOP:
            /* The kernel does not stop a service when its GUI closes, and
             * nothing else will reclaim the thread. DOOM without its screen is
             * nothing, so leave -- unlike a stopwatch, there is no state here
             * worth keeping resident.
             *
             * I had this hold on when the zone lives here, worried that
             * returning would free an arena the GUI was still using. That was
             * wrong twice over: GUI_STOP means the GUI *process* stopped --
             * suspension is GUI_SUSPEND, a different message -- and a service
             * that never returns leaves its region allocated for good, which
             * on a loader that grants under a megabyte per image means the app
             * cannot be launched a second time. */
            mGuiStarted = false;
#if UOOM_ZONE_IN_SERVICE
            svcLog(mKernel, "service: GUI stopped, releasing the zone\n");
#endif
            mKernel.comm.releaseMessage(msg);
            return;

        default:
            break;
        }

        if (msg->needsResponse()) {
            mKernel.comm.sendResponse(msg);
        }
        mKernel.comm.releaseMessage(msg);
    }

#if UOOM_ZONE_IN_SERVICE
    svcLog(mKernel, "service: message loop ended\n");
#endif
}

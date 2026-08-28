/* uoom_una_platform.cpp -- uoom_plat for the UNA Watch
 *
 * The whole platform dependency of the port, in one file. Everything above
 * uoom_plat.h is plain C that also runs on a laptop (host/), which is how the
 * port gets tested; this file is the part that can only be checked on the
 * watch.
 *
 * Written against the real SDK headers (una-sdk sdk-v1.4.0), not the docs.
 * The docs got several of these wrong and the corrections are noted in place,
 * because they are the kind of thing that gets re-broken.
 */

#include <cstring>
#include <cstdio>
#include <memory>

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Kernel/KernelProviderGUI.hpp"
#include "SDK/Interfaces/IFileSystem.hpp"
#include "SDK/Messages/CommandMessages.hpp"
#include "SDK/Messages/MessageGuard.hpp"
#include "SDK/Port/TouchGFX/TouchGFXCommandProcessor.hpp"

#include "UoomMessages.hpp"

extern "C" {
#include "uoom_plat.h"
#include "uoom_config.h"
#include "uoom_sys.h"       /* uoom_printf: the log channel that is readable */
}

namespace {

/* KernelProviderGUI::getKernel() returns a *const* reference on the GUI side
 * (the service provider returns non-const). Everything below is const-correct
 * against that -- comm/fs/sys members are references, so const on the Kernel
 * does not get in the way. */
const SDK::Kernel &kernel()
{
    return SDK::KernelProviderGUI::GetInstance().getKernel();
}

SDK::TouchGFXCommandProcessor &cmd()
{
    return SDK::TouchGFXCommandProcessor::GetInstance();
}

bool gQuit;
bool gSuspended;

/* One tick of the kernel's message pump.
 *
 * waitForFrameTick() only *queues* application-specific messages -- it pushes
 * them onto mUserQueue and returns on EVENT_GUI_TICK without ever draining
 * them. In a TouchGFX app the generated FrontendApplication::handleTickEvent()
 * calls callCustomMessageHandler(); nothing in the SDK itself does. So an app
 * that owns its own loop has to, and an app that forgets sees every custom
 * message from its own service sit in that queue forever.
 *
 * Which is exactly what happened here: the service reported sending the zone
 * grant four times and the GUI never saw one. */
void pumpOneTick()
{
    cmd().waitForFrameTick();
    cmd().callCustomMessageHandler();
}

}  /* namespace */

/* Set from UoomMain.cpp's lifecycle callbacks. */
extern "C" void uoom_una_set_quit(int q)      { gQuit = (q != 0); }
extern "C" void uoom_una_set_suspended(int s) { gSuspended = (s != 0); }
extern "C" int  uoom_una_suspended(void)      { return gSuspended ? 1 : 0; }

/* ---------------------------------------------------------------- storage
 *
 * IFile does have a seek -- the docs never published the interface, and the
 * one prose line that mentioned seek turned out to be right. It is absolute
 * only (`bool seek(size_t)`, no SEEK_CUR/SEEK_END, unsigned offset), which is
 * exactly what a WAD reader wants, so uoom_plat's offset-based read primitive
 * maps onto it one-for-one and the reopen-and-skip fallback this file used to
 * carry is gone.
 *
 * The signatures are not what the tutorials implied, though:
 *     bool read (char* buff, size_t btr, size_t& br);
 *     bool write(const char* buff, size_t btw, size_t& bw);
 * They return success, not a count, and hand back the transferred length in an
 * out-parameter. A short read is *success* with br < btr.
 */

struct uoom_plat_file {
    std::unique_ptr<SDK::Interface::IFile> f;
};

extern "C" uoom_plat_file_t *uoom_plat_open(const char *path, int write)
{
    /* A null path is a hard fault inside the filesystem wrapper, not an error
     * return -- and DOOM has at least one path that can produce one (the
     * savegame recovery fallback). Cheaper to check here than to find it again
     * from a watch that silently reboots. */
    if (path == nullptr || path[0] == '\0') {
        return nullptr;
    }

    auto f = kernel().fs.file(path);

    if (!f) {
        return nullptr;
    }
    /* open(wMode, override): override truncates. Writing a savegame wants a
     * clean file; reading must not create one. */
    if (!f->open(write != 0, write != 0)) {
        return nullptr;
    }

    /* The SDK's operator new is noexcept and returns nullptr on failure, and
     * it does not provide the (std::nothrow) overload -- so plain new plus a
     * null check is both correct and the only form that links. */
    auto *h = new uoom_plat_file();
    if (h == nullptr) {
        f->close();
        return nullptr;
    }
    h->f = std::move(f);
    return h;
}

extern "C" long uoom_plat_filesize(const char *path)
{
    SDK::Interface::IFileSystem::ObjectInfo info;

    /* IFile::size() stats the path and returns 0 on failure, which is
     * indistinguishable from an empty file. objectInfo() reports existence
     * separately, so use that. */
    if (!kernel().fs.objectInfo(path, info) || info.isDir) {
        return -1;
    }
    return static_cast<long>(info.size);
}

extern "C" int uoom_plat_pread(uoom_plat_file_t *h, uint32_t offset,
                               void *buf, uint32_t len)
{
    if (h == nullptr || !h->f) {
        return -1;
    }
    if (!h->f->seek(offset)) {
        return -1;
    }

    size_t got = 0;
    if (!h->f->read(static_cast<char *>(buf), len, got)) {
        return -1;
    }
    return static_cast<int>(got);
}

extern "C" int uoom_plat_write(uoom_plat_file_t *h, const void *buf, uint32_t len)
{
    if (h == nullptr || !h->f) {
        return -1;
    }

    size_t put = 0;
    if (!h->f->write(static_cast<const char *>(buf), len, put)) {
        return -1;
    }
    return static_cast<int>(put);
}

extern "C" void uoom_plat_sync(uoom_plat_file_t *h)
{
    if (h != nullptr && h->f) {
        h->f->flush();
    }
}

extern "C" void uoom_plat_close(uoom_plat_file_t *h)
{
    if (h != nullptr) {
        if (h->f) {
            h->f->flush();
            h->f->close();
        }
        delete h;
    }
}

extern "C" int uoom_plat_exists(const char *path)
{
    return kernel().fs.exist(path) ? 1 : 0;
}

extern "C" int uoom_plat_remove(const char *path)
{
    return kernel().fs.remove(path) ? 0 : -1;
}

extern "C" int uoom_plat_rename(const char *oldPath, const char *newPath)
{
    return kernel().fs.rename(oldPath, newPath) ? 0 : -1;
}

extern "C" int uoom_plat_mkdir(const char *path)
{
    return kernel().fs.mkdir(path) ? 1 : 0;
}

/* ------------------------------------------------------------------- time */

extern "C" uint32_t uoom_plat_ticks_ms(void)
{
    return kernel().sys.getTimeMs();
}

extern "C" void uoom_plat_delay_ms(uint32_t ms)
{
    kernel().sys.delay(ms);
}

/* ------------------------------------------------------------ frame pacing */

extern "C" void uoom_plat_frame_wait(void)
{
    /* Blocks until the kernel's EVENT_GUI_TICK, and -- more importantly --
     * this is where COMMAND_APP_STOP / GUI_SUSPEND / GUI_RESUME get handled.
     * Skipping it does not merely lose frame pacing: writeDisplayFrameBuffer
     * silently drops every frame until a RESUME has been dequeued, so a loop
     * that renders without pumping produces a permanently black screen.
     *
     * The tick is 10Hz (SDK::GUI::Config::kFrameRate), not the 30-60 the
     * TouchGFX port docs claim. */
    pumpOneTick();
}

extern "C" int uoom_plat_should_quit(void)
{
    return gQuit ? 1 : 0;
}

/* ---------------------------------------------------------------- display */

extern "C" void uoom_plat_present(const uint8_t *fb)
{
    /* Hands the kernel a raw pointer to our buffer and blocks (1s timeout)
     * while the kernel reads it, so the caller must not touch the buffer until
     * this returns -- which is how UOOM_FinishUpdate is sequenced.
     *
     * Partial updates do not exist: RequestDisplayUpdate's x/y/w/h are
     * "Reserved. Not used." and every frame is a full 57.6KB push. */
    if (!gSuspended) {
        cmd().writeDisplayFrameBuffer(fb);
    }
}

extern "C" void uoom_plat_keep_awake(void)
{
    /* The kernel's backlight auto-off is 4-5 seconds, which in a game means
     * the screen goes black mid-corridor. But RequestBacklightSet's
     * autoOffTimeoutMs is documented as "0 = disabled", so this is a one-shot
     * at startup rather than a re-arm every couple of seconds. Cheaper, and it
     * does not compete with the frame pushes for the message queue. */
    static bool sArmed;

    if (sArmed) {
        return;
    }
    sArmed = true;

    if (auto msg = SDK::make_msg<SDK::Message::RequestBacklightSet>(kernel())) {
        msg->brightness       = 100;    /* percent, not 0-255 */
        msg->autoOffTimeoutMs = 0;      /* stay lit while UOOM is running */
        msg.send(100);
    }
}

/* ------------------------------------------------------------------ input */

extern "C" int uoom_plat_poll_key(uint8_t *code)
{
    /* One code per call, FIFO, false when empty -- so the caller drains it in
     * a loop. That is the whole reason this port does not go through TouchGFX:
     * the SDK's own button controller samples exactly one code per frame tick,
     * and at 10Hz, with one physical press expanding to three codes, that
     * caps input at about three presses a second. */
    uint8_t k = 0;

    if (cmd().getKeySample(k)) {
        *code = k;
        return 1;
    }
    return 0;
}

extern "C" void uoom_plat_haptic(uint8_t strength)
{
    /* RequestVibroPlay goes straight to the kernel, so this does not need the
     * service process after all -- the effect ids are the DRV2605's own ROM
     * library. A gunshot wants a short sharp tick, not a buzz.
     *
     * Fire-and-forget: a dropped haptic is not worth a blocking send on the
     * frame path. */
    using Vibro = SDK::Message::RequestVibroPlay;

    if (auto msg = SDK::make_msg<Vibro>(kernel())) {
        msg->notesCount = 1;
        msg->notes[0].effect = (strength >= 60)
                                 ? Vibro::STRONG_CLICK_100
                                 : Vibro::SHARP_TICK_1_100;
        msg->notes[0].pause = 0;
        msg.send(0);
    }
}

/* ----------------------------------------------------------------- output */

extern "C" void uoom_plat_log(const char *msg)
{
    kernel().log.printf("%s", msg);
}

/* Filled in by UoomMain.cpp's customMessageHandler when the service replies.
 * Volatile because the reply is delivered from inside waitForFrameTick, i.e.
 * from the middle of the loop that is waiting for it. */
namespace {
volatile uint32_t gZoneAddr;
volatile uint32_t gZoneSize;
volatile bool     gZoneGranted;
}

extern "C" void uoom_una_zone_granted(uint32_t addr, uint32_t size)
{
    gZoneAddr    = addr;
    gZoneSize    = size;
    gZoneGranted = true;
}

extern "C" int uoom_plat_zone_from_service(uint32_t *addr, uint32_t *size)
{
    /* Ask, then pump until the answer arrives. The reply comes back through
     * waitForFrameTick -> callCustomMessageHandler, so the wait *must* be a
     * pump rather than a sleep, and the tick is 10Hz so a handful of ticks is
     * a fraction of a second. */
    const int kTries = 30;      /* ~3s at 10Hz */
    int i;

    gZoneGranted = false;

    if (!SDK::send_msg<UoomMessage::ZoneRequest>(kernel())) {
        uoom_printf("UOOM: zone request could not be sent\n");
        return 0;
    }

    uoom_printf("UOOM: asking the service for the zone\n");

    for (i = 0; i < kTries; ++i) {
        /* Logged every tick, deliberately. The first attempt at this handshake
         * produced a log that simply stopped after "asking" -- with no timeout
         * message either -- which said the process died inside this loop but
         * not how far in. */
        uoom_printf("UOOM: zone wait tick %d\n", i);
        pumpOneTick();
        if (gZoneGranted) {
            uoom_printf("UOOM: service answered after %d tick(s)\n", i + 1);
            if (addr != nullptr) {
                *addr = gZoneAddr;
            }
            if (size != nullptr) {
                *size = gZoneSize;
            }
            return 1;
        }
        /* Re-ask a few times: the service may not have reached its message
         * loop yet when the GUI starts. */
        if ((i % 10) == 9) {
            uoom_printf("UOOM: re-asking for the zone\n");
            SDK::send_msg<UoomMessage::ZoneRequest>(kernel());
        }
    }
    uoom_printf("UOOM: zone wait timed out after %d ticks\n", kTries);
    return 0;
}

extern "C" void uoom_plat_report_memory(uint32_t *largestFree)
{
    if (largestFree != nullptr) {
        *largestFree = 0u;
    }

    if (auto msg = SDK::make_msg<SDK::Message::RequestMemoryInfo>(kernel())) {
        if (msg.send(200) && msg.ok()) {
            uoom_printf(
                "UOOM heap: total %uK free %uK used %uK largest %uK frag %u%%\n",
                (unsigned)(msg->totalHeap / 1024u),
                (unsigned)(msg->freeHeap / 1024u),
                (unsigned)(msg->usedHeap / 1024u),
                (unsigned)(msg->largestFreeBlock / 1024u),
                (unsigned)msg->fragmentation);
            if (largestFree != nullptr) {
                *largestFree = msg->largestFreeBlock;
            }
            return;
        }
    }
    /* Silent: this firmware never answers, and a line every boot saying so is
     * noise in a log read off a watch. Kept as a query in case one does. */
}

extern "C" void uoom_plat_panic(const char *msg)
{
    /* uoom_fatal has already written this to the file; the kernel logger is
     * the only other channel and costs nothing to try. */
    kernel().log.printf("UOOM PANIC: %s\n", msg);

    /* The error screen is already on the panel (uoom_fatal drew it). Hold it
     * so it can be read -- at a 10Hz tick, 40 frames is four seconds -- then
     * leave. Exiting immediately would drop the user back to the watch face
     * with no idea what happened. */
    for (int i = 0; i < 40; ++i) {
        pumpOneTick();
    }
    kernel().sys.exit(-1);
    for (;;) {
    }
}

/* UoomMain.cpp -- the GUI process's entry point
 *
 * UOOM does not use TouchGFX, and the SDK turns out to make that easy rather
 * than hard -- but not the way the docs suggested.
 *
 * The SDK's own GUI entry point lives in
 * Libs/Source/AppSystem/EntryPoint/TouchGFX/main.cpp, which is a member of
 * UNA_SDK_SOURCES_GUI along with TouchGFXConfiguration.cpp -- and *that* file
 * defines touchgfx_init/touchgfx_taskEntry. So an app cannot simply override
 * those: it would collide with them and with `main` itself. Worse,
 * UNA_SDK_SOURCES_GUI is not even compilable without a TouchGFX Designer
 * project, since TouchGFXConfiguration.cpp includes generated headers
 * (BitmapDatabase.hpp, ApplicationFontProvider.hpp, FrontendHeap.hpp).
 *
 * The answer is to not link that group at all. We keep UNA_SDK_SOURCES_COMMON
 * -- which is what provides startup_user_app.s (the ENTRY(AppStart) the linker
 * script names), system.cpp (gIKernel, malloc, exit, the init arrays) and
 * KernelBuilder -- and supply this main() instead.
 *
 * One file from the TouchGFX group is still worth having, and it is the
 * important one: TouchGFXCommandProcessor.cpp pulls in no TouchGFX headers at
 * all despite its name. It carries the entire kernel-side protocol -- tick
 * wait, STOP handling, suspend/resume, the button queue, framebuffer submit --
 * so we compile that single file by path and get all of it for free.
 */

#include "SDK/Kernel/Kernel.hpp"
#include "SDK/Kernel/KernelBuilder.hpp"
#include "SDK/Kernel/KernelProviderGUI.hpp"
#include "SDK/Interfaces/IKernel.hpp"
#include "SDK/UnaLogger/Logger.h"

/* Brings in IGuiLifeCycleCallback and ICustomMessageHandler transitively; it
 * is TouchGFX-free despite the name. */
#include "SDK/Port/TouchGFX/TouchGFXCommandProcessor.hpp"

#include "UoomMessages.hpp"

extern "C" {
#include "uoom_config.h"
#include "uoom_input.h"

void uoom_run(void);                        /* doomgeneric_uoom.c */
void uoom_una_set_quit(int q);              /* uoom_una_platform.cpp */
void uoom_una_set_suspended(int s);
void uoom_una_zone_granted(uint32_t addr, uint32_t size);
}

/* Patched in by the app loader; declared in system.cpp's .sys_calls section. */
extern const SDK::Interface::IKernel *gIKernel;

namespace {

class UoomApp : public SDK::Interface::IGuiLifeCycleCallback,
                public SDK::Interface::ICustomMessageHandler
{
public:
    void onStart() override
    {
        /* Fires on the first frame tick, from inside waitForFrameTick(), by
         * which time uoom_run() is already in its loop. Nothing to do. */
    }

    void onStop() override
    {
        uoom_una_set_quit(1);
    }

    void onSuspend() override
    {
        /* The watch face is back. Two things must happen: stop pushing frames
         * (the kernel drops them while suspended anyway) and drop every held
         * button -- otherwise the player keeps walking into a wall in the
         * background and comes back to a corpse. */
        uoom_una_set_suspended(1);
        uoom_input_release_all();
    }

    void onResume() override
    {
        uoom_una_set_suspended(0);
    }

    void onFrame() override
    {
        /* uoom_run()'s loop is driven by waitForFrameTick() returning, not by
         * this callback. */
    }

    bool customMessageHandler(SDK::MessageBase *msg) override
    {
        if (msg->getType() == UoomMessage::ZONE_GRANT) {
            auto *g = static_cast<UoomMessage::ZoneGrant *>(msg);

            uoom_una_zone_granted(g->addr, g->size);
            return true;
        }
        /* Returning false lets the SDK mark the message FAIL rather than
         * silently swallowing it. */
        return false;
    }
};

}  /* namespace */

int main()
{
    /* Order matters and is copied from the SDK's own GUI main.cpp:
     * build the Kernel, publish it, then start logging. Anything that touches
     * KernelProviderGUI before CreateInstance -- including a global
     * constructor -- dereferences a null pointer.
     *
     * `kernel` is deliberately a local: the provider stores its address, and
     * this function never returns. */
    SDK::Kernel kernel = SDK::KernelBuilder::make(gIKernel);

    SDK::KernelProviderGUI::CreateInstance(&kernel);
    Logger_init(kernel.log);

    /* Constructed after the provider exists, because the command processor's
     * constructor reaches for the kernel. */
    static UoomApp app;
    auto &cmd = SDK::TouchGFXCommandProcessor::GetInstance();

    cmd.setAppLifeCycleCallback(&app);
    cmd.setCustomMessageHandler(&app);

    uoom_run();

    /* Returns when the kernel asked us to stop, or when there was no IWAD and
     * the user left. */
    kernel.sys.exit(0);
    for (;;) {
    }
}

/* Service.hpp -- UOOM's service process
 *
 * The SDK's service entry point (Libs/Source/AppSystem/EntryPoint/Service/
 * main.cpp) does `#include "Service.hpp"` and placement-news the object into
 * `alignas(Service) static uint8_t[sizeof(Service)]`, so the class has to be
 * complete here, and the constructor has to take `SDK::Kernel&`.
 *
 * Deliberately almost empty. The SDK's example apps put the work in the
 * service and keep the GUI thin; UOOM inverts that, because the framebuffer,
 * the frame tick and the buttons all live on the GUI side
 * (docs/02-architecture.md). Haptics turned out to be a plain kernel message
 * the GUI can send itself, so nothing is left here but existing.
 */
#ifndef UOOM_SERVICE_HPP
#define UOOM_SERVICE_HPP

#include "SDK/Kernel/Kernel.hpp"

#include "uoom_config.h"

class Service
{
public:
    explicit Service(SDK::Kernel &kernel);
    ~Service() = default;

    void run();

private:
#if defined(UOOM_ZONE_IN_SERVICE) && UOOM_ZONE_IN_SERVICE
    void grantZone();
#endif

    SDK::Kernel &mKernel;
    bool         mGuiStarted;
};

#endif /* UOOM_SERVICE_HPP */

/* UoomMessages.hpp -- the two custom messages UOOM needs
 *
 * IDs live in the application range the SDK reserves for app-internal traffic
 * (0x00000000-0x0000FFFF), numbered from 1 the way every shipped example app
 * does.
 *
 * Note the shape: a default constructor that sets the type tag, and a second
 * that fills the fields and delegates to it. That is what lets a caller use
 * SDK::send_msg<T>(kernel, args...) in one line, and it keeps the tag in a
 * single initialiser.
 */
#ifndef UOOM_MESSAGES_HPP
#define UOOM_MESSAGES_HPP

#include "SDK/Messages/MessageBase.hpp"

#pragma pack(push, 4)

namespace UoomMessage {

/* `SDK::MessageType::Type` is a uint32_t alias and MessageBase takes one
 * directly, so these are constants of that type rather than an enum class --
 * a scoped enum would not convert implicitly in `switch (msg->getType())`. */
constexpr SDK::MessageType::Type HAPTIC_PULSE = 0x00000001;
constexpr SDK::MessageType::Type GAME_STATE   = 0x00000002;
constexpr SDK::MessageType::Type ZONE_REQUEST = 0x00000003;
constexpr SDK::MessageType::Type ZONE_GRANT   = 0x00000004;

/* GUI -> Service. The haptic motor is a kernel-owned component and the GUI
 * process cannot reach it, so gunfire crosses the process boundary. */
struct HapticPulse : public SDK::MessageBase {
    uint8_t strength;       /* 0..100 */
    uint8_t durationMs10;   /* duration / 10, keeps the message tiny */

    HapticPulse()
        : SDK::MessageBase(HAPTIC_PULSE)
        , strength(0)
        , durationMs10(0)
    {}

    explicit HapticPulse(uint8_t s, uint8_t d10)
        : HapticPulse()
    {
        strength     = s;
        durationMs10 = d10;
    }
};

/* GUI -> Service. Lets the service decide whether it still has a reason to
 * exist: the kernel does not reap a service when its GUI closes. */
struct GameState : public SDK::MessageBase {
    uint8_t inGame;

    GameState()
        : SDK::MessageBase(GAME_STATE)
        , inGame(0)
    {}

    explicit GameState(uint8_t g)
        : GameState()
    {
        inGame = g;
    }
};

/* GUI -> Service, and the reply.
 *
 * DOOM's zone heap does not fit in the GUI process. The kernel's loader will
 * hand out somewhere between 878KB and 1009KB per app image (measured with
 * tools/probe-loader.sh), and the GUI needs ~600KB of that for DOOM's code and
 * static arrays -- leaving ~300KB for a zone that wants 700KB.
 *
 * But the service is a separate image with its own region and its own ceiling,
 * and there is no MMU: the SDK's platform overview states outright that "the
 * app can read entire MCU memory". So the zone lives in the service's .bss and
 * the GUI is handed its address. Both ELFs are position-independent and placed
 * by the loader, so the address has to be asked for at run time rather than
 * agreed at link time.
 *
 * Ugly. Also the difference between a port that runs and one that does not. */
struct ZoneRequest : public SDK::MessageBase {
    ZoneRequest()
        : SDK::MessageBase(ZONE_REQUEST)
    {}
};

struct ZoneGrant : public SDK::MessageBase {
    uint32_t addr;      /* runtime address of the service's zone array */
    uint32_t size;      /* its size in bytes */

    ZoneGrant()
        : SDK::MessageBase(ZONE_GRANT)
        , addr(0)
        , size(0)
    {}

    explicit ZoneGrant(uint32_t a, uint32_t n)
        : ZoneGrant()
    {
        addr = a;
        size = n;
    }
};

}  /* namespace UoomMessage */

#pragma pack(pop)

#endif /* UOOM_MESSAGES_HPP */

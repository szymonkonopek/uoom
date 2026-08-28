# UNA Watch SDK, as far as it is published

> **Superseded on the points that matter. Read
> [`una-sdk-verified.md`](una-sdk-verified.md) first** — it is read from the
> actual source at `sdk-v1.4.0` and corrects this document on `IFile`'s
> interface, the frame rate (10 Hz, not 30-60), what `RAM_LENGTH` means, where
> the heap lives, which libc symbols exist, and how to own the GUI task.
>
> This file is kept deliberately: it is the best reading available from the
> public documentation alone, and the size of the gap between the two is itself
> a useful thing to know before trusting a docs-only design.

Condensed from <https://www.developers.unawatch.com/latest/> for the parts a
DOOM port has to call. **Every ⚠️ marks something the docs do not actually
say** — several things a port needs are simply not published, and one of them
(the `IFile` interface) is a project-level risk. Those are tracked in
`docs/07-open-questions.md`.

## The five facts that decide the port

| Fact | Value |
|---|---|
| Display / framebuffer | 240×240, **1 byte/px ABGR2222**, one static `uint8_t sFrameBuffer[57600]`, software rendering, no DMA, no double buffer |
| Frame rate | ⚠️ **contradictory**: the TouchGFX Port page says "typically 30-60 FPS"; the Stopwatch and Images pages both say **10 Hz** |
| App RAM | `UNA_APP_SERVICE_RAM_LENGTH` **500K**, `UNA_APP_GUI_RAM_LENGTH` **600K**, both settable, **no documented maximum** |
| File seek | ⚠️ **No `IFile` class is documented anywhere.** One prose line claims seek exists |
| Simulator | Windows (VS / TouchGFX Designer) **or x86-64 Linux (GCC + SDL2)**; runs the whole app including the service |

## 1. Service process

### What the SDK provides

From a real tutorial build log, the files compiled into *your* service ELF:

```
Software/Libs/Sources/Service.cpp
Libs/Source/AppSystem/AtExitImpl.cpp
Libs/Source/AppSystem/startup_user_app.s
Libs/Source/AppSystem/system.cpp
Libs/Source/Kernel/KernelBuilder.cpp
Libs/Source/UnaLogger/Logger.cpp
Libs/Source/AppSystem/EntryPoint/Service/main.cpp
```

So `main()` is **the SDK's**, the reset vector is `startup_user_app.s`, and
`SDK::Kernel` is assembled by `KernelBuilder.cpp`. You write only
`Service.cpp` with a `Service(SDK::Kernel&)` constructor and a `void run()`.

⚠️ The HelloWorld tutorial contains **no C++ at all** — only shell commands.
The published skeletons come from the Example-app pages.

### The canonical shape (Stopwatch, verbatim)

```cpp
class Service
{
public:
    Service(SDK::Kernel &kernel);
    virtual ~Service() = default;
    void run();

private:
    SDK::Kernel     &mKernel;
    Stopwatch::Core  mStopwatch;
    bool             mGuiStarted;

    void handleCommand(SDK::MessageBase *msg);
    void publish();
};
```

### The message loop

```cpp
void Service::run() {
    MessageBase* msg = nullptr;
    while (comm->getMessage(msg, 100)) {      // 100ms timeout
        switch (msg->getType()) {
            case MessageType::EVENT_SENSOR_LAYER_DATA: ...
            case MessageType::COMMAND_APP_STOP: ...
        }
        if (msg->needsResponse()) comm->sendResponse(msg);
        comm->releaseMessage(msg);            // always
    }
}
```

Stopwatch blocks with an **infinite** timeout ("no polling loop and no timer");
HRMonitor uses 500 ms so a periodic check runs twice a second.

### Lifecycle messages

- `COMMAND_APP_STOP` — forced exit.
- `COMMAND_APP_NOTIF_GUI_RUN` — the GUI is up.
- `COMMAND_APP_NOTIF_GUI_STOP` — the GUI is gone.

⚠️ **The kernel does not stop a service when its GUI closes**, and "nothing
else will ever reclaim the thread". A service exits by **returning from
`run()`**. The GUI side exits with `mKernel.sys.exit()`
(`ISystem::exit(int status = 0)`, documented as "*No return*"), or
`presenter->exit()` from view code.

⚠️ **`IGuiBackend` is not an app API.** The only definition in the docs is the
*kernel's* `Backend` class interface. No example app implements it — every one
does GUI↔Service with custom messages.

⚠️ `#include` paths are almost entirely unpublished. Headers live under
`Libs/Header/SDK/`, so paths are `SDK/<Area>/<File>.hpp`. Confirmed:
`SDK/Messages/CommandMessages.hpp`, `SDK/GUI/Button.hpp`,
`SDK/Port/TouchGFX/TouchGFXCommandProcessor.hpp`,
`SDK/Port/TouchGFX/TouchGFXHAL.hpp`, `SDK/AppConfig/AppConfig.hpp`. Get the
rest with `ls -R $UNA_SDK/Libs/Header/SDK/`.

## 2. GUI process

Entry points, in the SDK's GUI `main.cpp`:

- `touchgfx_init()` — initialise TouchGFX, register resources
- `touchgfx_taskEntry()` — main GUI task entry point (**infinite loop**)
- `touchgfx_components_init()` — currently empty

The class you write is `Model`:

```cpp
class Model : public touchgfx::UIEventListener,
              public SDK::Interface::IGuiLifeCycleCallback,
              public SDK::Interface::ICustomMessageHandler
{ ... };

Model::Model()
    : modelListener(nullptr)
    , mKernel(SDK::KernelProviderGUI::GetInstance().getKernel())
{
    SDK::TouchGFXCommandProcessor::GetInstance().setAppLifeCycleCallback(this);
    SDK::TouchGFXCommandProcessor::GetInstance().setCustomMessageHandler(this);
}
```

`IGuiLifeCycleCallback` gives `onStart()`, `onStop()`, `onResume()`,
`onSuspend()`, `onFrame()`.

### Buttons — the critical table

Codes are **printable ASCII**, three per physical button.
`Gui::Config::Button::L1` etc. are only the *click* column.

| Button | click | press | release |
|---|---|---|---|
| **SW1 (L1)** | `'1'` | `'q'` | `'a'` |
| **SW2 (R1)** | `'3'` | `'e'` | `'d'` |
| **SW3 (L2)** | `'2'` | `'w'` | `'s'` |
| **SW4 (R2)** | `'4'` | `'r'` | `'f'` |

Note the crossover: **SW2 = R1, SW3 = L2**.

```cpp
struct EventButton : public MessageBase {
    enum class Id : uint8_t { SW1 = 0, SW2, SW3, SW4 };
    enum class Event : uint8_t {
        PRESS = 0, RELEASE, CLICK, LONG_PRESS, HOLD_1S, HOLD_5S, HOLD_10S
    };
    uint32_t timestamp;
    Id       id;
    Event    event;
};
```

`LONG_PRESS` and `HOLD_*` are **not forwarded** — "a screen derives long press
from the press/release pair with its own timing".

⚠️ Two ceilings that matter for a game:
- "*Button input delayed by message processing (~1-2ms). Additionally, buttons
  are processed through a **50-60ms debounce filter and react only upon
  release**.*"
- The queue is `FixedQueue<uint8_t, 16>` and the SDK's own
  `HWButtonController::sample()` pops **exactly one code per frame tick**. At
  10 Hz, with one press expanding to three codes, that is about three presses a
  second. **Draining `getKeySample()` yourself is not an optimisation, it is
  necessary.**

## 3. Custom messages

| Range | Purpose |
|---|---|
| **`0x00000000-0x0000FFFF`** | **application-specific** |
| `0x01010000-0x01060000` | kernel→app commands |
| `0x02010000-0x020A0000` | system / hardware requests |
| `0x03010000-0x03040000` | events |

⚠️ Some tutorials use `0x00010001`, outside that range. Every shipped example
uses `0x00000001` upward; do that.

⚠️ **Do not use an `enum class`** for the IDs: `SDK::MessageType::Type` is a
`uint32_t` alias and a scoped enum will not convert implicitly in
`switch (msg->getType())`.

The canonical struct — a default constructor that sets the tag, and a second
that fills fields and delegates:

```cpp
struct HRValues : public SDK::MessageBase {
    float heartRate;
    float trustLevel;

    HRValues() : SDK::MessageBase(HR_VALUES), heartRate(), trustLevel() {}

    explicit HRValues(float heartRate, float trustLevel) : HRValues() {
        this->heartRate  = heartRate;
        this->trustLevel = trustLevel;
    }
};
```

⚠️ **All message structs must be `#pragma pack(push, 4)`** for alignment with
the message pool. `MessageBase` overhead is **32 bytes**; pools are
size-classed (a 134-byte message lands in a 256-byte class; the largest
documented example is 1024 bytes).

Sending:

- `SDK::send_msg<T>(kernel, args...)` — allocate, construct, send, release.
  **Posts with a zero timeout**: never waits, and a message that finds no room
  is **dropped**. Fire-and-forget.
- `SDK::make_msg<T>(kernel, args...)` — returns an RAII `MessageGuard`; use
  `msg.send(timeout) && msg.ok()` and read fields back through `msg->`.
  ⚠️ `MessageGuard` is published as a bare declaration with **zero documented
  methods**; the API above is known only from tutorial usage.

`IAppComm` rules, verbatim from the API reference:

- `getMessage(msg, timeoutMs = 0xFFFFFFFF)` — "*Application must call
  `releaseMessage()` after processing*".
- `sendResponse(msg)` — only valid when `needsResponse()`; "*Does NOT release
  message*".
- `releaseMessage(msg)` — "*After this call, message pointer becomes
  invalid*".
- `sendMessage(msg, timeoutMs = 0)` — must be a message from
  `allocateMessage`. With `timeoutMs > 0`, `true` means a response arrived;
  check `msg->getResult()`.
- `static void *operator new(size_t) = delete` — **messages can only come from
  kernel pools**, and are therefore non-copyable. (The Waypoint tutorial notes
  the consequence: keep your own plain struct if the GUI needs to remember the
  last update.)

GUI-side receive queue is `FixedQueue<MessageBase*, 10>`, drained only at
frame-tick time, **oldest dropped on overflow**. At 10 Hz that is ~100
messages/second before loss. Do not stream pixels through it.

## 4. Filesystem

```cpp
class IFileSystem {
    virtual bool mkdir(const char* path) = 0;                       // and parents
    virtual std::unique_ptr<IFile> file(const char* path) = 0;
    virtual std::unique_ptr<IDirectory> dir(const char* path) = 0;
    virtual bool exist(const char* path) const = 0;
    virtual bool remove(const char* path) = 0;
    virtual bool rename(const char* oldPath, const char* newPath) = 0;
    virtual bool copy(const char* oldPath, const char* newPath) = 0;
    virtual bool objectInfo(const char* path, ObjectInfo& item) const = 0;

    static constexpr size_t skMaxPathLen = 256;   // including '\0'
};

struct ObjectInfo {
    char   name[skMaxPathLen];
    bool   isDir, isHidden, isSystem;
    size_t size;        // the only documented way to learn a file's length
    time_t utc;
};
```

### ⚠️⚠️ `IFile` — seek is not documented

- The published API index lists `IFileSystem`, `IAppComm`, `IAppMemory`,
  `IKernel`, `ILogger`, `ISystem`. **No `IFile`, no `IDirectory`.** In the API
  reference the string `IFile` appears exactly once — as the return type of
  `file()`.
- The only methods ever demonstrated, from the Files tutorial:

```cpp
auto file = mKernel.fs.file("settings.json");
if (file && file->open(0)) {          // open(bool wMode = false) -- 0 reads
    size_t bytesRead = file->read(buffer, sizeof(buffer) - 1);
    file->close();
}
// open(1) writes, and truncates: "Overwrites entire file on each save"
```

- The single mention of seek anywhere: "*File Operations: Standard
  **read/write/seek** via IFile interface.*"

Underneath is a "FatFs Wrapper (577 lines)" with `f_open`/`f_read`, so
`f_lseek` almost certainly exists — but the public interface is unpublished.
**Read `$UNA_SDK/Libs/Header/SDK/Interfaces/` before writing WAD code.**

### Volumes and the working directory

```
0:/  internal flash   system files
1:/  external flash   user data
2:/  USB storage      media / apps
3:/  SPI flash        backup / logs
```

exFAT, **4 KB cluster size**. The app's working directory is its own folder on
`2:/` — `2:/Apps/<AppName>/`. Every example opens `"settings.json"` with no
volume prefix. In the simulator the sandbox is a host directory whose path the
GUI logs at startup ("Path to files created by app").

### Getting a 4 MB WAD across

**USB MSC** — documented deployment flow: connect, *wait for mass storage to
attach* (running apps may need to flush first), drop files into `Apps/<name>/`,
eject, power-cycle.

**BLE File Transfer Service** — service `0000FEBB-…`, characteristics
`ADAF0001` (version) / `ADAF0002` (raw transfer), bonded encrypted connection
required, ≤ **201 bytes per notification** at MTU 220. Tens of thousands of
round trips for 4 MB. Use MSC.

## 5. Memory

```cpp
class IAppMemory {
    virtual void *malloc(size_t size) = 0;
    virtual void free(void *ptr) = 0;
    virtual void *realloc(void *ptr, size_t size) = 0;
};
```

That is the entire documented interface — no `calloc`, no alignment control, no
free-bytes query.

⚠️ **Whether plain `malloc`/`new` are allowed is never stated**, but: "*All apps
share the same standard library implementation provided by the kernel… Full
support for modern C++ features (strings, vectors, etc.)*"; example apps use
`new` and `std::vector` directly; and "*The kernel tracks and cleans up
user-created dynamic allocations to prevent leaks*". Working conclusion:
`malloc`/`new` work and are backed by the kernel's per-app heap.
`new (std::nothrow)` is recommended.

| CMake variable | Default |
|---|---|
| `UNA_APP_SERVICE_STACK_SIZE` | `10*1024` |
| `UNA_APP_SERVICE_RAM_LENGTH` | `500K` |
| `UNA_APP_GUI_STACK_SIZE` | `10*1024` |
| `UNA_APP_GUI_RAM_LENGTH` | `600K` |

These land in the per-app linker script `${APP_NAME}Service.ld`, whose contents
are not published.

⚠️ **No MMU.** The platform overview states it outright: "*No MMU, so the app
can read entire MCU memory and execute whatever it want. Developers need to
ensure that apps do not interfere with each other or the kernel.*" No MPU
enforcement is documented. (The deep dive says the opposite; the platform
overview is the more specific claim.)

⚠️ A deep-dive diagram claims `App Memory / RAM: 256KB`, contradicting the
600 K default. Treat that whole diagram — including "Code Segment Flash:
512KB", "RTOS Heap RAM: 32KB" — as illustrative.

⚠️ Total SRAM on the STM32U595 appears **nowhere** in the docs. Check ST's
datasheet.

## 6. Display

```cpp
// TouchGFXHAL.cpp:53-64
static const int16_t skWidth = 240;
static const int16_t skHeight = 240;
static const uint32_t skBufferSize = skWidth * skHeight;  // 57,600

static uint8_t sFrameBuffer[skBufferSize];
static uint8_t* spActiveBuffer;
static bool sFlushBufferReq;
```

**ABGR2222**: "*each pixel uses 2 bits per channel (Alpha, Blue, Green, Red)*"
— high bits alpha, low bits red, **64 usable colours**. The SDK's own icon
converter is `png2abgr2222.py`, which "reduces each RGBA channel to 2 bits,
packs into single byte" (and rotates 90° clockwise).

Constraints, verbatim: single static buffer, software rendering only; **DMA not
supported (stub)**; touch **not implemented (stub)**; GPIO not utilised;
"*No Double Buffering*"; "*Fixed Resolution: Currently optimized for 240×240
displays only*". The buffer size is compile-time in the **SDK**, not your app.

### The one API a software renderer needs

```cpp
class TouchGFXCommandProcessor {
public:
    static TouchGFXCommandProcessor& GetInstance();
    void setAppLifeCycleCallback(IGuiLifeCycleCallback* cb);
    void setCustomMessageHandler(ICustomMessageHandler* h);
    bool waitForFrameTick();
    bool getKeySample(uint8_t& key);
    void writeDisplayFrameBuffer(const uint8_t* data);
    void callCustomMessageHandler();
};

void TouchGFXCommandProcessor::writeDisplayFrameBuffer(const uint8_t* data) {
    if (!data || !mIsGuiResumed) return;
    auto* msg = mKernel.comm.allocateMessage<SDK::Message::RequestDisplayUpdate>();
    if (msg) {
        msg->pBuffer = data;
        mKernel.comm.sendMessage(msg, 1000);   // 1s timeout
        mKernel.comm.releaseMessage(msg);
    }
}

struct RequestDisplayUpdate : public MessageBase {
    const uint8_t* pBuffer;
    int16_t x, y;            // "unused, always full screen"
    int16_t width, height;   // "unused, always 0 for full update"
};
```

It takes **an arbitrary pointer**, so an app can hand it its own 57 600-byte
buffer. ⚠️ It **blocks** while the kernel reads that buffer — do not mutate
until it returns. ⚠️ **Partial updates do not exist**: every frame is a full
57.6 KB push. No documented ban on calling it from app code; it is public, and
hand-rolling the message would only lose the `mIsGuiResumed` guard.

`getTFTFrameBuffer()` is `protected` and returns `uint16_t*` — a TouchGFX
inherited signature, not a usable app API.

### Frame tick

```cpp
void OSWrappers::waitForVSync() {
    SDK::TouchGFXCommandProcessor::GetInstance().waitForFrameTick();
}
```

`waitForFrameTick()` blocks on `getMessage`, dispatches queued custom messages,
and returns on `EVENT_GUI_TICK`.

⚠️ Rate is contradictory. **10 Hz** is the better-sourced number: the Stopwatch
page documents choosing tenths-of-a-second precision *because* "a hundredths
digit would be finer than the screen can move", and the Images page says
"60 ticks (~6s at 10Hz)". The "30-60 FPS" claim is vaguer prose in a
performance section. **Plan for 10 and measure.**

### Backlight

`SDK::Message::RequestBacklightSet`. Real usage: brightness 100% with a
**5-second** auto-off (Running) or **4000 ms** (Alarm). ⚠️ **A game will go
dark mid-corridor unless it re-arms the backlight.**

### Suspend / resume

```
Running --> Suspended: COMMAND_APP_GUI_SUSPEND
Suspended --> Running: COMMAND_APP_GUI_RESUME
Running --> [*]: COMMAND_APP_STOP
```

`writeDisplayFrameBuffer` silently no-ops while suspended.

## 7. Build system

The real shape, from the tutorials and example apps:

```cmake
set(APP_NAME "Files")
set(APP_TYPE "Activity")          # Activity | Utility | Glance | Clockface
set(DEV_ID "UNA")
set(APP_ID "F1E2D3C448669786")    # 16 uppercase hex

include($ENV{UNA_SDK}/cmake/una-app.cmake)
include($ENV{UNA_SDK}/cmake/una-sdk.cmake)

set(SERVICE_SOURCES ${LIBS_SOURCES} ${UNA_SDK_SOURCES_COMMON}
                    ${UNA_SDK_SOURCES_APPSYSTEM} ${UNA_SDK_SOURCES_JSON})
una_app_build_service(${APP_NAME}Service.elf)

set(GUI_SOURCES ${TOUCHGFX_SOURCES} ${UNA_SDK_SOURCES_COMMON}
                ${UNA_SDK_SOURCES_GUI})
una_app_build_gui(${APP_NAME}GUI.elf)

una_app_build_app()
```

Sources go into the magic list variables **before** the build calls. Available
groups: `UNA_SDK_SOURCES_COMMON`, `_APPSYSTEM`, `_JSON`, `_SERVICE`, `_GUI`,
plus `LIBS_SOURCES` (globbed from `LIBS_PATH`) and `TOUCHGFX_SOURCES`. They are
à la carte — Stopwatch deliberately omits the FIT, sensor and calibration
groups. A Glance app calls `una_app_build_service` and `una_app_build_app` with
no GUI target at all.

⚠️ The `una_add_app(NAME ... GUI_SOURCES ...)` form that appears in the
TouchGFX Port page matches **no real app**. Use the form above.

**`.c` files work** — a tutorial build log shows
`Building C object .../coreJSON/source/core_json.c.obj`, and ASM too
(`startup_user_app.s`). Good news for a DOOM port.

Other variables: `APP_AUTOSTART`, `APP_USER_NAME` (name on the watch),
`APP_FILE_NAME` (basename of the `.uapp` — the phone's install flow keys on
it), `LIBS_PATH` `../../Libs`, `OUTPUT_PATH` `../../../Output`,
`RESOURCES_PATH` `../../../Resources`, `TOUCHGFX_PATH` `../TouchGFX-GUI`,
`BUILD_VERSION` (from git, e.g. `0.1.1-22-bf09e2b`).

`syscalls.cpp` sits in the per-app CMake directory and is otherwise
undocumented; given the toolchain warning about "*undefined syscall stubs
(`_write`, `_close`, etc.)*", it is the newlib retargeting layer. Read it — it
tells you exactly which libc facilities are wired up.

### Packaging

`app_packer.py -e <elf> -o <dir> -v <ver>` per ELF → `.srv` / `.gui`
(parses sections, extracts **ARM relocations**, builds a UAPP header, appends
CRC32; requires 4-byte-aligned sections). Then `app_merging.py` combines them
with the two icons (converted to ABGR2222, rotated 90°) into the `.uapp`.
A minimal GUI app is **~225 KB**.

⚠️ Icons `Resources/icon_30x30.png` and `icon_60x60.png` are **required**.
⚠️ On Windows the final copy into `Output/` uses a wildcard `cmake -E copy`
does not expand — copy it yourself.
⚠️ CMake does **not** regenerate TouchGFX code; that is TouchGFX Designer's job.

## 8. Simulator

**Linux build needs no ARM toolchain:**

```bash
sudo apt-get install -y build-essential libsdl2-dev libsdl2-image-dev \
                        libjpeg-dev ruby ruby-nokogiri
UNA_SDK=/abs/path/to/una-sdk make -f simulator/gcc/Makefile -j"$(nproc)"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build/bin/simulator.out
```

**The service really runs in it** — the docs warn that "in the simulator the
service is constructed before TouchGFX's HAL exists", which is what the logger
writes through, so logging that early segfaults it. File I/O is real, backed by
a dirent FileSystem into a host directory.

Mocked: `Mock::SystemService`, `Mock::SystemGUI`, `Mock::Logger`,
`Mock::FileSystem`, `Mock::AppMemory`, plus `Mock.Buzzer`, `Mock.Backlight`,
`Mock.Vibro`. ⚠️ `IAppComm` is **not** mocked — the real IPC path runs.

Keyboard: `1`=L1, `2`=L2, `3`=R1, `4`=R2, `5`=simulated wrist detection.
⚠️ Those are the **click** codes; it is not clear the simulator can inject the
press/release codes a game needs.

⚠️ **The simulator lists its sources by hand** in `simulator/gcc/Makefile` and
`simulator/msvs/Application.vcxproj` — unlike the CMake target. For a port with
eighty `.c` files, that is a second source list to maintain. This is a large
part of why UOOM has its own host harness instead.

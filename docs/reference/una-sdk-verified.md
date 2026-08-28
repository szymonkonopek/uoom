# UNA SDK, verified against source

Read from `github.com/UNAWatch/una-sdk` at `8cdb731` (tag `sdk-v1.4.0`). **This
file overrides `una-sdk-api.md`**, which was assembled from the public
documentation before the source was available; that document is kept because
the gap between the two is itself worth knowing, and it is flagged at its top.

Paths are relative to the SDK root.

## The corrections that mattered

| | The docs said | The source says |
|---|---|---|
| `IFile` | not documented at all; one prose line claimed seek exists | **`seek` exists**, plus `size`, `getPosition`, `truncate`, `flush`, `isOpen` — but `read`/`write` return `bool` with an out-param, not a count |
| Frame tick | "typically 30-60 FPS" | **10 Hz**, as a real constant with a doc comment |
| `RAM_LENGTH` | read as an allocation | a **link-time ceiling only**; the loader allocates from real section sizes |
| Flash | assumed code lives in flash | **there is no flash region** — `.text` and `.rodata` are in RAM too |
| `malloc` | "probably backed by the kernel heap" | confirmed, and that heap is **outside `RAM_LENGTH`** |
| libc | assumed the toolchain's | **`libc.a`, `libm.a` and `libgcc.a` are all `/DISCARD/`ed** |
| Own GUI task | unclear whether possible | possible, but **only by not linking `UNA_SDK_SOURCES_GUI` at all** |
| Haptics | "message not documented" | `RequestVibroPlay`, with the DRV2605 effect library |

## 1. `IFile` — the complete interface

`Libs/Header/SDK/Interfaces/IFileSystem.hpp:172-250`. Namespace is
`SDK::Interface` (singular — the closing comment saying `Interfaces` is stale).
All three interfaces live in that one header; there is no `IFile.hpp`.

```cpp
class IFile : public IFsObject {
public:
    virtual ~IFile() = default;
    virtual size_t size() const = 0;
    virtual bool open(bool wMode = false, bool override = false) = 0;
    virtual bool isOpen() const = 0;
    virtual bool close() = 0;
    virtual bool read(char* buff, size_t btr, size_t& br) = 0;
    virtual bool write(const char* buff, size_t btw, size_t& bw) = 0;
    virtual bool seek(size_t offset) = 0;
    virtual bool truncate(size_t offset) = 0;
    virtual bool flush() = 0;
    virtual size_t getPosition() const = 0;
};
```

`IFsObject` (`:125-164`) adds `setPath`, `getPath`, `exist`, `rename`, `remove`.

What a WAD reader needs to know:

- **`seek` is absolute only.** `size_t`, unsigned — no `SEEK_CUR`, no
  `SEEK_END`, no negative offsets. Which is exactly what DOOM's `w_file`
  abstraction wants, since it reads at absolute offsets.
- **`read` returns success, not a count.** A short read is *success* with
  `br < btr`. There is no `eof()` and no error code.
- `buff` is `char*`, not `void*` — casts required.
- `open(wMode, override)`: `!wMode` → `O_RDONLY`; `wMode && override` →
  `O_RDWR|O_CREAT|O_TRUNC`; `wMode && !override` → `O_RDWR|O_CREAT` positioned
  at the start, deliberately *not* `O_APPEND`, so `seek` + `write` works.
- `size()` stats the path, so it works on a closed file — but returns `0` on
  failure, indistinguishable from an empty file. Use
  `IFileSystem::objectInfo()` when you need to tell those apart.
- **No memory-mapped or XIP file access anywhere in the SDK.** So DOOM's
  zero-copy `wad_file_t::mapped` path — the trick that makes the sub-256KB
  ports possible — is not available.

## 2. `SDK::Kernel`

`Libs/Header/SDK/Kernel/Kernel.hpp:35-65`. A class, five **reference** members,
all short names:

```cpp
SDK::Interface::ISystem     &sys;
SDK::Interface::ILogger     &log;
SDK::Interface::IAppMemory  &mem;
SDK::Interface::IAppComm    &comm;
SDK::Interface::IFileSystem &fs;
```

`sys` not `system`, `log` not `logger`, `mem` not `memory`. There is **no**
`backlight`, `vibro`, `buzzer`, `settings` or `time` member: those interfaces
have headers but no `IKIP::IntfID`, so they are unreachable from an app.
Backlight, haptics and buzzer are **messages only** (§5).

- GUI: `SDK::KernelProviderGUI::GetInstance().getKernel()` returns
  **`const SDK::Kernel&`**.
- Service: `SDK::KernelProviderService::GetInstance().getKernel()` returns
  non-const.
- Both store a raw pointer and `getKernel()` dereferences it **with no null
  check**. Anything that touches it before `CreateInstance` — including a
  global constructor — faults.

## 3. Owning the GUI task

`touchgfx_init`, `touchgfx_components_init` and `touchgfx_taskEntry` are
defined in
`Libs/Source/Port/TouchGFX/generated/TouchGFXConfiguration.cpp`, which is a
member of `UNA_SDK_SOURCES_GUI` (`cmake/una-sdk.cmake:72-82`) — **as is `main`
itself** (`Libs/Source/AppSystem/EntryPoint/TouchGFX/main.cpp`). So overriding
those symbols while linking that group is a duplicate-definition error twice
over, and `--gc-sections` does not help: duplicates are diagnosed before GC.

That group is also **not compilable without a TouchGFX Designer project** —
`TouchGFXConfiguration.cpp:19-22` includes `BitmapDatabase.hpp`,
`ApplicationFontProvider.hpp`, `FrontendHeap.hpp`, `TypedTextDatabase.hpp`, all
app-generated.

**So the way to own the render loop is to not link the group.** Keep
`UNA_SDK_SOURCES_COMMON` (`una-sdk.cmake:4-11`) — `startup_user_app.s` (the
`ENTRY(AppStart)` the linker script names), `system.cpp` (`gIKernel`, `malloc`,
`exit`, init arrays), `KernelBuilder.cpp`, `Logger.cpp`, `AtExitImpl.cpp`,
`Timer.cpp` — and write your own `main()`. The SDK's, which yours must mirror:

```cpp
int main()
{
    SDK::Kernel kernel = SDK::KernelBuilder::make(gIKernel);
    SDK::KernelProviderGUI::CreateInstance(&kernel);
    Logger_init(kernel.log);
    touchgfx_components_init();
    touchgfx_init();
    touchgfx_taskEntry();   // No return
    return 0;
}
```

`kernel` is a stack local in `main` whose address is published globally — legal
only because `main` never returns. Keep that property.

**The one file worth borrowing:** `TouchGFXCommandProcessor.cpp` includes **no
TouchGFX headers** despite its name (`.hpp:16-25` is `<cstdint> <cstddef>
<queue>` plus `SDK/...`). Compiling that single file by path gives you the whole
kernel-side protocol: tick wait, STOP handling, suspend/resume, the button
queue, framebuffer submit. This is the highest-value reuse in the SDK for a
custom-renderer port.

There is also an unused, undocumented `EntryPoint/CustomGUI/main.cpp` that
expects a `class Gui` with `Gui(SDK::Kernel&)` and `run()` — referenced by no
CMake variable and no doc.

## 4. Memory — the finding that reframes the budget

`UNA_APP_{GUI,SERVICE}_RAM_LENGTH` becomes exactly one thing, a linker
`--defsym` (`cmake/una-app.cmake:247-254`, `:309-317`), feeding the **only**
MEMORY region (`Libs/Source/AppSystem/linker/Main/Sections.ld:7-10`):

```
MEMORY
{
  RAM (xrw) : ORIGIN = 0x20000000,  LENGTH = RAM_LENGTH
}
```

**There is no flash region.** `.text`, `.rodata`, `.data`, `.got`, `.bss` and
`.stack` are all `> RAM` — apps execute from SRAM, and DOOM's code and its
`const` trig tables count against this number exactly like `.bss` does.

**But it is a ceiling, not a reservation.** `app_packer.py:200-266` records
real per-section sizes in the `UAPP` header and the loader allocates from
those; `RAM_LENGTH` appears nowhere in the image. There is **no validation, no
clamp and no maximum** anywhere in the SDK — an exhaustive grep finds only the
`--defsym` and a `message()`. Its own defaults total 1100K across two
concurrently-loaded processes, which no single-part SRAM budget could reserve.

So: **raise it freely; it costs nothing at run time.** The real limit is
whatever the kernel's app loader can carve out, and that loader is not in this
repository.

**And the heap is outside it entirely.** `_sbrk` is a hard trap
(`system.cpp:158-163`, `assert(0 && "_sbrk must not be used in user app")`);
`malloc`/`new` forward to the kernel's allocator via `IAppMemory`
(`:220-226`, `:412-415`). There is no `.heap` section. Consequence for a DOOM
port: **the zone heap belongs in `malloc`, not in `.bss`** — the opposite of
the usual bare-metal advice. `SDK::Message::RequestMemoryInfo`
(`CommandMessages.hpp:263-279`) reports `totalHeap`, `freeHeap`, `usedHeap`,
`largestFreeBlock`, `fragmentation` at run time; the SDK never states the size.

**Trap worth spelling out:** `app_packer.py:229-233` copies only nine known
section names and merely *warns* about anything else. A custom
`__attribute__((section("MyBlob")))` builds clean and arrives as garbage on the
watch. Also, `sh_size % 4 != 0` on any of the nine is a hard packer error.

## 5. libc: what is actually there

`Sections.ld:1` includes `LibC/libc_exports_0.0.3.ld`, which is 336
`PROVIDE(sym = 0x080xxxxx)` lines binding symbols to absolute addresses in the
kernel's flash-resident newlib. And `Sections.ld:140-145`:

```
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }
```

**Present:** `memcpy memmove memset memchr memcmp memalign strcat strchr strcmp
strcpy strlen strncat strncmp strncpy strrchr strstr strtok strtok_r strtod
strtof strtol strtoll strtoul strtoull printf snprintf sprintf siprintf siscanf
vsnprintf tolower toupper isalnum isalpha isdigit isspace isinf isnan strerror`,
the whole of libm, and `time gmtime_r localtime_r mktime gettimeofday clock`.

**Absent, and DOOM wants every one of these:** `strcasecmp`, `strncasecmp`,
`abs`, `atoi`, `puts`, `putchar`, `qsort`, `rand`, `srand`, `fprintf`, `fopen`,
`fwrite` — **and every `__aeabi_*` / libgcc helper.**

That last clause is the sharp one. Cortex-M33 has hardware `SDIV`/`UDIV`, so
32-bit division is fine, but **64-bit division calls `__aeabi_ldivmod`, which
does not exist** — and `m_fixed.c`'s `FixedDiv` does
`((int64_t) a << 16) / b` **per column of the frame**. You cannot fix it with
`-lgcc`: the `/DISCARD/` pattern matches the archive basename regardless of
path. UOOM supplies `uoom_libc.c` and patch 0011 for exactly this.

`-mfpu=fpv5-sp-d16` is **single**-precision hardware; `double` arithmetic calls
libm (exported) and libgcc (not). Note also there is no `syscalls.cpp` in the
repo — `system.cpp` plays that role — and `_write` is **not** implemented, so
`printf` to a console is unusable. Logging goes through `kernel.log`.

Build flags (`una-app.cmake:94-131`): `-nostdlib -nodefaultlibs -nostartfiles
-static -mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -Os -fPIC
-ffunction-sections -fdata-sections -fstack-usage -Wall -Wl,--gc-sections
-Wl,--emit-relocs`, plus `-fno-exceptions -fno-rtti -fno-use-cxa-atexit` for
C++. No `nano.specs`, no `-u _printf_float`. `-fPIC` is mandatory — the loader
relocates through the GOT with `r9` as base.

`operator new` is `noexcept` and returns `nullptr` on failure; with
`-fno-exceptions`, a `std::vector` growth failure lands in
`std::__throw_bad_alloc` → `assert(false)` → `exit(-1)`. Null-check every
allocation.

## 6. The frame tick is 10 Hz

`Libs/Header/SDK/GUI/Config.hpp:20-26`:

```cpp
/**
 * @brief Kernel tick rate delivered to GUI applications (frames per second).
 *
 * The kernel calls the application's tick handler this many times per second.
 * Use this value when computing durations in ticks.
 */
inline constexpr uint32_t kFrameRate = 10;
```

Every example app derives its timings from it, and the simulators pin vsync to
`1000.0f / kFrameRate`. The "30-60 FPS" claims are stale prose in
`Docs/TouchGFX-Port-Architecture.md` and a comment on the message struct; no
code uses them.

`EVENT_GUI_TICK` is *generated* by the kernel firmware, which is not in this
repo. `EventGuiTick` carries `frameNumber` and `timestamp`, but
`waitForFrameTick()` releases the message without exposing either — use
`kernel.sys.getTimeMs()`.

`waitForFrameTick()` (`TouchGFXCommandProcessor.cpp:39-152`) is an infinite
message pump that returns only on `EVENT_GUI_TICK`, or never on
`COMMAND_APP_STOP` (it calls `onStop()` then `sys.exit(0)`). On device it blocks
with an infinite `getMessage` timeout; the 50 ms variant is `#if
defined(SIMULATOR)`.

## 7. The messages a game needs

All in `SDK::Message`, `#pragma pack(push, 4)`, deriving the 32-byte
`MessageBase`. Send with `SDK::send_msg<T>(kernel, ...)` (fire-and-forget) or
`SDK::make_msg<T>(kernel, ...)` for an RAII guard with `.send(timeout)`,
`.ok()`, `operator->`.

**Backlight** — `CommandMessages.hpp:341-353`:

```cpp
struct RequestBacklightSet : public MessageBase {
    uint8_t brightness;         // 0-100%, 0 = off
    uint32_t autoOffTimeoutMs;  // Auto-off timeout, 0 = disabled
};
```

`brightness` is a **percentage**, not 0-255. And `autoOffTimeoutMs = 0`
disables auto-off, so a game can arm the backlight once at startup instead of
re-arming every couple of seconds.

**Haptics** — `RequestVibroPlay` (`:385-430`), up to 8 notes, effect ids from
the DRV2605's ROM library: `STRONG_CLICK_100 = 1`, `SHARP_CLICK_100 = 4`,
`SOFT_BUMP_100 = 7`, `DOUBLE_CLICK_100 = 10`, `STRONG_BUZZ_100 = 14`,
`SHARP_TICK_1_100 = 24`, `PULSING_STRONG_1_100 = 52`, and more. `Note` is
`{ uint8_t effect; uint32_t pause; }` — `effect = 0` means the note is a pause.
**This goes straight to the kernel, so the GUI process can fire it directly**;
no service round-trip needed.

**Buzzer** — `RequestBuzzerPlay` (`:360-380`), up to 10 notes of
`{ uint32_t time; uint8_t volume; }`. **No frequency field** — duration and
four volume levels (0/33/66/100). There is no PCM path anywhere in the SDK, so
DOOM's sound effects and music are not portable; the buzzer is a blip
generator.

**Display** — `RequestDisplayUpdate`'s `x/y/width/height` are documented
"Reserved. Not used. Always update the entire buffer." Every frame is a full
57 600-byte push, sent blocking with a 1000 ms timeout.
`writeDisplayFrameBuffer` **silently no-ops unless `mIsGuiResumed`**, which is
set only when `COMMAND_APP_GUI_RESUME` has been dequeued — so a loop that
renders without pumping messages produces a permanently black screen that looks
like a driver bug.

## 8. Buttons

`Libs/Header/SDK/GUI/Button.hpp:35-51`. Kernel id → position:
`SW1→L1, SW2→R1, SW3→L2, SW4→R2`.

| | click | press | release |
|---|---|---|---|
| L1 | `'1'` | `'q'` | `'a'` |
| L2 | `'2'` | `'w'` | `'s'` |
| R1 | `'3'` | `'e'` | `'d'` |
| R2 | `'4'` | `'r'` | `'f'` |

Plus one the public docs never mentioned: **`L1R2 = 'z'`**, a built-in chord
code. UOOM's own chord is L1+L2, but it expands `'z'` into both buttons anyway,
because if the firmware sends it *instead of* the individual codes then
dropping it would eat a turn or a shot in the middle of combat — which is
exactly when L1+R2 happens.

`getKeySample(uint8_t&)` pops one code per call, FIFO, `false` when empty — so
**drain it in a loop**. Capacity is 16 and overflow drops the oldest. The SDK's
own controller samples one code per frame tick, which at 10 Hz with three codes
per physical press caps input at about three presses a second.

`LONG_PRESS` and `HOLD_*` are deliberately not forwarded; derive long press
from the press/release pair yourself.

## 9. Build system

`una_app_build_service(TARGET)` / `una_app_build_gui(TARGET)` take one
positional argument and read everything else **from the caller's scope by
name**: `SERVICE_SOURCES` / `GUI_SOURCES`, `*_INCLUDE_DIRS`, `TOUCHGFX_LIBS`,
the RAM/stack vars, `BUILD_VERSION`, `APP_NAME`, `DEV_ID`, `APP_ID`.

`una_app_build_app()` takes **no arguments** and hard-codes the target names
`${APP_NAME}Service.elf` and `${APP_NAME}GUI.elf`. It only appends the GUI ELF
to its dependencies **if `TOUCHGFX_PATH` is defined** — so an app with a custom
renderer must still define that variable or its GUI is never merged into the
`.uapp`.

Include root for `SDK/...` headers: **`$ENV{UNA_SDK}/Libs/Header`**
(`UNA_SDK_INCLUDE_DIRS_COMMON`). `UNA_SDK` must be an *environment* variable.

Source groups: `COMMON` (6 files, always needed), `APPSYSTEM` (only the
service `main.cpp`), `GUI` (9 files — the ones to omit for a custom renderer),
`JSON`, `FIT`, `SENSOR`, `TRACKMAP`, `VARIANT`, `APPCONFIG`, `CALIBRATION`, and
`SERVICE` as an aggregate of most of those. `.c` and `.s` files can be appended
to the source lists freely; C is enabled with `CMAKE_C_STANDARD 11`.

## 10. Things that break a custom render loop

1. **Pump the message queue every frame or render nothing** — see §7's note on
   `mIsGuiResumed`.
2. **10 Hz pacing is the kernel's, not yours.** A DOOM loop expecting
   `TICRATE == 35` gets ~10 display updates a second; decide how many game
   tics to run per display tick.
3. **No watchdog kick, no keep-alive, no app timeout, no app-facing MPU
   config.** The only mandatory periodic behaviour is #1.
4. Custom ELF sections are silently dropped (§4).
5. Everything runs from SRAM, code and rodata included (§4).
6. Missing libc/libgcc symbols surface only at link (§5).
7. Static-init guards are non-atomic and `__cxa_atexit` is disabled — safe only
   because the app is single-threaded. There is no thread API.
8. **No touch** — `STM32TouchController::sampleTouch` returns `false`
   unconditionally. Four buttons is the whole input surface.

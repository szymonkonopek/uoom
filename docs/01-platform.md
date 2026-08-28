# The target machine

Everything here is taken from the public UNA Watch SDK documentation at
<https://www.developers.unawatch.com/latest/>. Where the docs are silent or
contradict themselves, that is called out -- those gaps are the ones that will
decide whether this port is comfortable or painful, and they are tracked in
`docs/07-open-questions.md`.

## Hardware

| | | Source |
|---|---|---|
| MCU | **STM32U595** (Cortex-M33) | Architecture Deep Dive, static component diagram |
| Internal flash | 2 MB | same |
| External storage | eMMC, FATFS | same; Deep Dive filesystem section (`f_open`/`f_read`/`f_readdir`) |
| Display | 240 x 240, 8 bpp **ABGR2222** | TouchGFX Port Architecture |
| LCD driver | `LS012B7DD06A` | Deep Dive, ComponentFactory |
| Touch | GT911 present in hardware, **stubbed in software** | Deep Dive / TouchGFX Port ("*stub implementation - not implemented*") |
| Buttons | 4 (SW1..SW4) | Deep Dive |
| Haptics | DRV2605 | Deep Dive |
| Buzzer | PWM | Deep Dive |
| BLE | BlueNRG-2 | Deep Dive |
| GPS | Airoha AG3335M | Deep Dive |
| RTOS | FreeRTOS | Deep Dive |

### The panel, from its own datasheet

The SDK never describes the display beyond "240x240 ABGR2222". Sharp's spec for
`LS012B7DD06A` (LCP-2619063C) fills in what a graphics port actually needs, and
two of these are load-bearing:

| Property | Value |
|---|---|
| Technology | Sharp **Memory-in-Pixel**, reflective, transflective (T~0.95%) |
| Interface | **6-bit parallel**, 21-pin FPC. **Not SPI**, no command set, no addressing |
| Resolution | 240 x 240, RGB stripe |
| **Active area** | a **circle**, diameter 30.24 mm = radius **120 px**. The corners of the 240x240 grid are not visible |
| Native colours | **64** -- 2 bits per subpixel by spatial area gradation |
| Frame rate | **30 Hz typical, 33 Hz max**; ~25 ms of data time per frame |
| Power | 11 uW hold, ~500 uW at 30 Hz update |

Do not confuse it with `LS012B7DD01`, which is a 184x38 monochrome SPI part;
several software projects have. The 8-colour memory-in-pixel panels people
associate with this class of watch are JDI's, not this one -- **this display is
genuinely 64-colour**, which happens to match ABGR2222 exactly.

Two practical notes: there is **no `EXTCOMIN` pin and no internal COM option**
on this part -- VCOM/VA/VB are three externally generated phase-locked square
waves that must keep swinging even in hold mode -- and the datasheet warns
against leaving a static image up for more than two days.
| USB | MSC + VCP stack | Deep Dive, communication layer |

The one number the docs never state is **how much SRAM the chip has**, which is
unfortunate because it is the number this port lives or dies by. ST's
STM32U59x/5Ax line is the large-SRAM member of the U5 family, and the app RAM
allowances below (500 K + 600 K by default, adjustable) are only sensible on a
part with megabytes rather than hundreds of kilobytes. **Verify against the
datasheet for the exact part before trusting the memory budget.**

Documented contradiction: the Deep Dive's component diagram labels the display
`LCD Display 320x300 RGB`, while the TouchGFX Port Architecture -- which is the
document that actually describes the framebuffer -- says 240x240 ABGR2222 with a
`uint8_t sFrameBuffer[57600]`, and the Images tutorial says "*typically
240x240*". Two of three agree and the framebuffer size arithmetic
(240*240*1 = 57600) settles it. UOOM targets **240x240x8bpp**, with the panel
geometry isolated in `uoom_config.h` in case that turns out to be wrong.

## Software stack

- Apps are **compiled ELF binaries** loaded by the kernel -- not a
  scripting runtime. Two ELFs per app (Service + GUI), packed into a `.uapp`.
- C++ with the **ST** ARM GCC toolchain (from STM32CubeIDE or STM32CubeCLT).
  The docs are emphatic that a distro `gcc-arm-none-eabi` is "*frequently
  incompatible*" -- missing newlib syscall stubs such as `_write`. Plain C
  sources build fine alongside; DOOM needs that.
- CMake 3.21+, driven by `una-sdk.cmake` / `una-app.cmake` from the SDK
  checkout that `$UNA_SDK` points at.
- GUI is **TouchGFX**, software-rendered. `STM32DMA` is a stub -- there is no
  DMA2D acceleration to lean on.

## What reading the SDK source changed

The SDK is public (`github.com/UNAWatch/una-sdk`, read at `sdk-v1.4.0`), and it
contradicts the documentation on several points that matter to a game.
`docs/reference/una-sdk-verified.md` has the quotes; the short version:

- **Apps execute from SRAM.** The linker script has a single MEMORY region and
  **no flash region** — `.text` and `.rodata` sit in RAM with `.bss`.
- **`RAM_LENGTH` is a linker ceiling, not a reservation**, with no cap and no
  validation anywhere. Raising it costs nothing at run time.
- **`malloc` is the kernel's allocator and its heap is outside that region.**
  `_sbrk` is a hard trap.
- **`libc.a`, `libm.a` and `libgcc.a` are all discarded** by the linker script;
  336 symbols are bound to addresses in the kernel's own newlib. `strcasecmp`,
  `abs`, `atoi`, `puts` and **every `__aeabi_*` helper** are absent — which
  makes a 64-bit division a link error.
- **The frame tick is 10 Hz**, as a real constant (`SDK::GUI::Config::kFrameRate`),
  not the 30-60 the TouchGFX port document claims.
- **There is no touch**, confirmed in code: `sampleTouch` returns `false`
  unconditionally.

## Memory allowances

The numbers that matter, from the SDK setup page and the tutorial build logs:

| CMake variable | Default |
|---|---|
| `UNA_APP_SERVICE_RAM_LENGTH` | `500K` |
| `UNA_APP_GUI_RAM_LENGTH` | `600K` |
| `UNA_APP_SERVICE_STACK_SIZE` | `10*1024` |
| `UNA_APP_GUI_STACK_SIZE` | `10*1024` |

These become linker `--defsym RAM_LENGTH` / `STACK_SIZE` and feed the single
MEMORY region in `Libs/Source/AppSystem/linker/Main/Sections.ld`. They are
**ceilings, not reservations** — see above — and the Deep Dive's diagram
claiming `App Memory / RAM: 256KB` is illustrative, not normative.

Apps also get an explicit allocator interface, `IAppMemory`
(`malloc` / `free` / `realloc`), and `malloc`/`new` route to it. Its heap size
is not stated anywhere in the SDK; `RequestMemoryInfo` reports it at run time.

## Frame and input timing

- Frames are kernel-driven: `EVENT_GUI_TICK` -> `waitForFrameTick()`,
  documented as "*typically 30-60 FPS*".
- `RequestDisplayUpdate` carries a **raw pointer** to a 240x240x1 buffer and is
  sent with a 1 s timeout. Update rectangles exist in the struct but are
  documented as "*unused, always full screen*".
- Buttons: "*~1-2ms*" of message latency, plus "*a 50-60ms debounce filter*",
  and they "*react only upon release*". See `docs/05-input.md` -- this is the
  single most consequential sentence in the SDK docs for a game.
- The frame tick is **10 Hz** (`SDK::GUI::Config::kFrameRate`, verified in
  source). The panel itself manages 30 Hz, so this is a kernel choice rather
  than a hardware limit.

## Simulator

There is a TouchGFX Designer / Visual Studio simulator, **Windows-only**, with
the four buttons on keys `1`-`4`. The SDK also ships host GoogleTest harnesses
(`Tests/Host`) and `SDK::Simulator::Mock::*` implementations of `ILogger`,
`IFileSystem`, `IAppMemory` and `ISystem` -- which means the service-side
interfaces are mockable on a desktop. That is the seam UOOM's own host harness
(`host/`) imitates, so most of this port can be exercised on macOS or Linux
without a watch and without Windows.

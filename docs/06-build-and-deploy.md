# Building, flashing, and getting a WAD onto the watch

## 0. What you need

| | |
|---|---|
| UNA SDK checkout | `git clone https://github.com/UNAWatch/una-sdk`; `$UNA_SDK` must point at its root, as an **environment** variable |
| ST ARM GCC | from **STM32CubeIDE** or **STM32CubeCLT**. Not your distro's `gcc-arm-none-eabi` -- the SDK docs warn it is "*frequently incompatible*" (missing newlib syscall stubs such as `_write`) |
| CMake | 3.21+ |
| `make` | shipped with CubeIDE; **CubeCLT does not include it** |
| Python 3 | for the SDK's packaging scripts |
| An IWAD | see below. Not included, not distributable |

```sh
export UNA_SDK=/path/to/una-sdk
export PATH="$HOME/.local/share/stm32cube/bundles/gnu-tools-for-stm32/*/bin:$PATH"
which arm-none-eabi-gcc     # must resolve, and be the ST one
```

On Windows, dot-source the SDK's helper instead:
`. ./una-sdk/Utilities/Scripts/export-stm32-tools.ps1`

## 1. Vendor DOOM

```sh
tools/fetch-doomgeneric.sh
```

Clones `ozkl/doomgeneric` at a pinned commit into `third_party/doomgeneric/`
and applies anything in `tools/patches/`. It is not committed here: it is
GPLv2 upstream code we do not own, and pinning + patching keeps our diff
against it visible.

## 2. Build the port layers on your laptop first

```sh
tests/run.sh
```

Compiles `uoom_video` and `uoom_input` for four configurations (scaled/native,
dithered/flat, fill/fit) and runs the behavioural tests. **Do this before every
device build.** It takes under a second and it catches the class of bug that is
miserable to find on a watch with no debugger attached.

## 3. Build for the watch

```sh
tools/build-watch.sh          # or: tools/build-watch.sh clean
```

That wraps three things that are easy to get wrong and unpleasant to diagnose:

- **The toolchain, CMake and make all come from STM32CubeCLT.** The system ones
  are not interchangeable — the SDK's docs are blunt about it, and the ST fork
  is what the SDK's own probe for `-fcyclomatic-complexity` expects.
- **`UNA_SDK` must be an environment variable**, not a `-D`. Default here is
  `../una-sdk`.
- **The SDK's packaging scripts need `pyelftools` and `pillow`**, and on a
  current macOS or Linux those cannot go into the system Python (PEP 668). The
  script keeps a project-local `.venv` and points the SDK at it with
  `-DUNA_PYTHON_EXECUTABLE`.

Verified output:

```
UNA_SDK   = /path/to/una-sdk
toolchain = /opt/ST/STM32CubeCLT_1.22.0/GNU-tools-for-STM32/bin/arm-none-eabi-gcc
[100%] Merging UOOM application
INFO:root:Image : Output/UOOM_0.0.0-dev.uapp (305088 bytes)
Total             597159
```

The `.uapp` lands in `Output/`. The hundreds of
`Forcing branch to absolute symbol in Thumb mode` warnings are inherent to the
SDK's linker script, which binds libc to absolute addresses in the kernel's
flash; the script filters them out.

Raw form, if you would rather drive it yourself:

```sh
export UNA_SDK=/path/to/una-sdk
export PATH="/opt/ST/STM32CubeCLT_1.22.0/GNU-tools-for-STM32/bin:\
/opt/ST/STM32CubeCLT_1.22.0/CMake/bin:/opt/ST/STM32CubeCLT_1.22.0/Make/bin:$PATH"
cmake -G "Unix Makefiles" -DUNA_PYTHON_EXECUTABLE=$PWD/.venv/bin/python \
      -S Software/Apps/UOOM-CMake -B build-watch
cmake --build build-watch -j8
```

If CMake cannot find the compiler even with `$PATH` set -- a documented Windows
quirk -- add:

```sh
cmake -G "Unix Makefiles" \
  -DCMAKE_SYSTEM_NAME=Generic \
  -DCMAKE_C_COMPILER=arm-none-eabi-gcc \
  -DCMAKE_CXX_COMPILER=arm-none-eabi-g++ \
  -DCMAKE_ASM_COMPILER=arm-none-eabi-gcc \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
  -S Software/Apps/UOOM-CMake -B build
```

Clean rebuild: `rm -rf build` and repeat both steps.

### Build knobs

Pass with `-D` at configure time; they map onto `uoom_config.h`:

| | |
|---|---|
| `-DUOOM_RENDER_MODE=1` | render natively at 240x240 (faster, less RAM, clipped status bar) |
| `-DUOOM_SCALE_MODE=1` | letterbox 240x150 instead of filling the panel |
| `-DUOOM_DITHER=0` | no ordered dither -- flat 64-colour quantisation |
| `-DUOOM_ZONE_BYTES=...` | DOOM's zone heap, requested from the kernel allocator. The first thing to tune |
| `-DUOOM_LOG_MAP_ALLOC=1` | print every level-geometry array's size at load |
| `-DUOOM_HUD_DIAG=1` | frame time and WAD read count in the corner |

## 4. Check the map file before you flash

The build is going to be RAM-bound long before it is flash-bound, and the
linker is the only honest source on that:

```sh
arm-none-eabi-size -A build/UOOMGUI.elf
arm-none-eabi-nm --size-sort -S build/UOOMGUI.elf | tail -30
tools/struct-sizes.sh          # DOOM's structures at ARM sizes, not the host's
```

Remember that on this platform `.text` and `.rodata` are in RAM too — there is
no flash region in the app's linker script — so the `size` output is the whole
story, not just the `.bss` line.

The second command lists the biggest symbols, which is exactly the list in
`docs/03-memory-budget.md`. If something unexpected is near the top, a DOOM
module that should have been excluded got linked in.

## 4b. Icons

`Resources/icon_30x30.png` and `icon_60x60.png` are required by the packer and
generated from `Resources/src/uoom-logo.jpeg`:

```sh
tools/make_icons.py
```

Worth knowing why that is a tool rather than two committed PNGs: the packer
converts icons to ABGR2222 by **truncating** each channel to its top two bits,
which turns a dark metallic wordmark reddish (150,120,90 becomes 170,85,85).
The tool quantises first, leaving every channel an exact multiple of 85, so the
packer's truncation is lossless and the result is the one that was chosen by
looking at it.

It also autocrops the black field, boosts contrast before quantising, and
deliberately does **not** dither -- at 30 pixels the ordered dither that
rescues DOOM's light ramps scatters the letter shapes instead.

The two sizes use different art, because they have to: at 60x60 the full
wordmark resolves into four legible letters, while at 30x30 four letters across
30 pixels is just texture, so the small icon is a single "U". A size-specific
`Resources/src/icon30.*` or `icon60.*` wins over the shared `uoom-logo.*`. All
of it is explained at the top of the tool.

## 5. Get the WAD onto the watch

Shareware `DOOM1.WAD` is committed at [`wad/DOOM1.WAD`](../wad/), so a fresh
clone runs. `tools/fetch-wad.sh` fetches into the same directory --
Freedoom by default, `--shareware` for id's `DOOM1.WAD` -- and checks what it
got, because a WAD that quietly differs from the one the numbers in
[`03-memory-budget.md`](03-memory-budget.md) came from would make every
comparison here a lie.

| Source | File | Licence |
|---|---|---|
| [Freedoom](https://freedoom.github.io/) ([0.13.0](https://github.com/freedoom/freedoom/releases/download/v0.13.0/freedoom-0.13.0.zip), 24 MB, both phases) | `freedoom1.wad` | BSD. Nothing to think about |
| [`doom1.wad`](https://github.com/Akbar30Bill/DOOM_wads/raw/master/doom1.wad) (4 196 020 B, MD5 `f0cefca4…`) | `Doom1.WAD` | id's shareware release, re-hosted by a third party. Ready to use |
| [`doom19s.zip`](https://www.gamers.org/pub/idgames/idstuff/doom/doom19s.zip) (2 450 688 B) | — | The same shareware release from the idgames archive: id's original 1995 DOS installer. Its `DOOMS_19.1/.2` are DEICE-compressed, so getting the WAD out needs DOSBox or a deice extractor |
| Your own copy | `DOOM.WAD`, `DOOM2.WAD` | Registered. **Not** redistributable |

On the licence: id distributed the shareware DOOM for free copying, which is why
it is still on public archives and in Linux distribution repositories. Those
terms cover the *complete, unmodified* shareware package, and a bare extracted
WAD is not literally that -- which is why this repository links to a copy rather
than carrying one.

There are two shareware `DOOM1.WAD` builds in circulation, both 4 196 020 bytes
with 1264 lumps. The committed one is v1.9; see [`wad/README.md`](../wad/) for
why that matters to demo playback.

| Version | MD5 |
|---|---|
| v1.9 | `f0cefca49926d00903cf57551d901abe` |
| v1.8 | `5f4eb849b1af12887dec04a2a12e5e62` |

Where it goes: `uoom/DOOM1.WAD` on the watch's storage (`UOOM_WAD_DIR`), or
next to the `.uapp` in the app's own directory. `uoom_find_iwad()` tries the
`uoom/` subdirectory first, then the app directory, and prefers a real IWAD over
Freedoom if both are present.

**Which IWAD you use is the single biggest factor in whether this port fits.**
Shareware `DOOM1.WAD` needs a ~1 MB zone; Freedoom Phase 1 needs ~2 MB, because
its maps are roughly four times the geometry. See `docs/03-memory-budget.md`.

How it gets there -- the SDK documents two paths, and which one is practical
for a 4 MB file is an open question (`docs/07-open-questions.md`):

- **USB MSC.** The kernel has a USB mass-storage stack; if the watch presents
  its filesystem when plugged in, this is a drag-and-drop and by far the best
  option for a file this size.
- **BLE File Transfer Service.** Documented as a BLE service. At BLE
  throughput a 4 MB file is minutes, not seconds.

## 6. Install and run

The SDK's documented deployment flow, which also carries the WAD:

1. Connect the watch over USB and **wait for mass storage to attach** — running
   apps may need to flush first, so it is not instant.
2. Create `Apps/UOOM/` on the watch drive.
3. Copy `Output/UOOM_*.uapp` into it.
4. Copy your IWAD in as well — either `Apps/UOOM/uoom/DOOM1.WAD` or
   `Apps/UOOM/DOOM1.WAD`. `uoom_find_iwad()` tries both.
5. Eject the drive safely and disconnect.
6. **Power-cycle the watch** — off, then on.
7. Top-right button, then find UOOM in the app list.

`APP_AUTOSTART` is `Off` — you would not want DOOM launching itself on
wrist-raise.

### What the first boot should tell you

UOOM logs three things before it draws anything, and together they close the
last open questions in `docs/07-open-questions.md`:

```
UOOM heap: total ..K free ..K used ..K largest ..K frag ..%
zone: ...... bytes
UOOM: iwad DOOM1.WAD
```

The first line is `RequestMemoryInfo`, and it is the only way to learn the size
of the kernel's heap — where DOOM's zone lives, and a number the SDK documents
nowhere. If `largest` comes back smaller than `UOOM_ZONE_BYTES`, UOOM asks for
that instead of stepping down 64 KB at a time, and says so on the second line.

If no IWAD is found you get an instruction screen with the path it looked at
rather than a black screen. Build with `-DUOOM_HUD_DIAG=1` to put the frame
time and WAD read count in the corner — that answers whether the tick really
is the documented 10 Hz.

On first run, if no WAD is found, UOOM draws an instruction screen with the path
it looked at rather than exiting silently. That is the one piece of UI this port
has.

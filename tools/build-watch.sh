#!/bin/sh
# Build UOOM for the watch. Wraps the three-step setup that is easy to get
# wrong and hard to diagnose:
#
#   1. the ST toolchain, CMake and make all come from STM32CubeCLT -- the
#      distro/system ones are not interchangeable;
#   2. UNA_SDK must be an *environment* variable pointing at a una-sdk checkout;
#   3. the SDK's packaging scripts need pyelftools and pillow, which on a modern
#      macOS or Linux cannot be pip-installed into the system Python (PEP 668),
#      so this keeps a project-local venv and points the SDK at it.
#
# Usage:
#   tools/build-watch.sh                 build
#   tools/build-watch.sh clean           wipe the build dir first
#   tools/build-watch.sh --smoke         platform bring-up only, no DOOM
#   tools/build-watch.sh --ballast=1536  smoke plus 1536K of static .bss, to
#                                        probe the loader (tools/probe-loader.sh)
#   tools/build-watch.sh --svc-zone=640  full game with a 640K zone (the zone
#                                        lives in the service process by
#                                        default -- see UoomMessages.hpp)
#   UNA_SDK=... CLT=... tools/build-watch.sh
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)

# ---------------------------------------------------------------- toolchain
CLT=${CLT:-$(ls -d /opt/ST/STM32CubeCLT_* 2>/dev/null | sort -V | tail -1)}
if [ -z "$CLT" ] || [ ! -d "$CLT/GNU-tools-for-STM32/bin" ]; then
    echo "STM32CubeCLT not found. Install it, or set CLT=/path/to/STM32CubeCLT_x.y.z" >&2
    exit 1
fi
PATH="$CLT/GNU-tools-for-STM32/bin:$CLT/CMake/bin:$CLT/Make/bin:$CLT/Ninja/bin:$PATH"
export PATH

# ------------------------------------------------------------------ the SDK
UNA_SDK=${UNA_SDK:-$ROOT/../una-sdk}
if [ ! -f "$UNA_SDK/cmake/una-app.cmake" ]; then
    echo "UNA_SDK does not look like a una-sdk checkout: $UNA_SDK" >&2
    echo "  git clone https://github.com/UNAWatch/una-sdk" >&2
    exit 1
fi
UNA_SDK=$(cd "$UNA_SDK" && pwd)
export UNA_SDK

# ------------------------------------------------------- the SDK's python deps
VENV=$ROOT/.venv
if [ ! -x "$VENV/bin/python" ]; then
    echo "creating $VENV for the SDK's packaging scripts"
    python3 -m venv "$VENV"
fi
"$VENV/bin/python" -c "import elftools, PIL" 2>/dev/null || {
    echo "installing pyelftools and pillow into $VENV"
    "$VENV/bin/pip" install --quiet pyelftools pillow
}

# ---------------------------------------------------------------- the sources
[ -f third_party/doomgeneric/doomgeneric/d_main.c ] || tools/fetch-doomgeneric.sh

# --smoke builds the platform bring-up only: no WAD, no zone, no DOOM linked.
# See Software/Libs/Sources/uoom_smoke.c -- it is the bisection tool for a
# device that shows nothing, because a 13KB app that also shows nothing rules
# out both DOOM and the memory budget.
SMOKE=
BALLAST=
SVCBALLAST=
SVCZONE=
for a in "$@"; do
    [ "$a" = "--smoke" ] && SMOKE=ON
    case "$a" in
        --ballast=*)     BALLAST=${a#--ballast=}; SMOKE=ON ;;
        --svc-ballast=*) SVCBALLAST=${a#--svc-ballast=}; SMOKE=ON ;;
        --svc-zone)      SVCZONE=ON ;;
        --svc-zone=*)    SVCZONE=${a#--svc-zone=} ;;
    esac
done

if [ -n "$SMOKE" ]; then
    BUILD=$ROOT/build-smoke
    CMAKE_EXTRA="-DUOOM_SMOKE=ON"
    [ -n "$BALLAST" ] && CMAKE_EXTRA="$CMAKE_EXTRA -DUOOM_BALLAST_KB=$BALLAST"
    [ -n "$SVCBALLAST" ] && CMAKE_EXTRA="$CMAKE_EXTRA -DUOOM_SVC_BALLAST_KB=$SVCBALLAST"
else
    BUILD=$ROOT/build-watch
    CMAKE_EXTRA=""
    # The service-held zone is the default; --svc-zone=N only picks its size.
    if [ -n "$SVCZONE" ] && [ "$SVCZONE" != "ON" ]; then
        CMAKE_EXTRA="-DUOOM_ZONE_KB=$SVCZONE"
    fi
fi

for a in "$@"; do
    [ "$a" = "clean" ] && rm -rf "$BUILD"
done

echo "UNA_SDK   = $UNA_SDK"
echo "toolchain = $(command -v arm-none-eabi-gcc)"

# The ELFs land next to the CMake project, so a switch between the full and
# smoke configurations must not reuse a stale one.
rm -rf Software/Apps/UOOM-CMake/build

cmake -G "Unix Makefiles" $CMAKE_EXTRA \
      -DUNA_PYTHON_EXECUTABLE="$VENV/bin/python" \
      -S Software/Apps/UOOM-CMake -B "$BUILD" >/dev/null

# The "Forcing branch to absolute symbol" warnings are inherent to the SDK's
# linker script, which binds libc to absolute addresses in the kernel's flash.
cmake --build "$BUILD" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" 2>&1 \
  | grep -vE "Forcing branch to absolute symbol"

echo
echo "=== footprint (every byte of this is RAM: the app linker script has no flash region) ==="
# CMAKE_RUNTIME_OUTPUT_DIRECTORY puts the ELFs next to the CMake project.
ELFDIR=Software/Apps/UOOM-CMake/build
arm-none-eabi-size -A "$ELFDIR/UOOMGUI.elf"
echo
echo "=== the twelve biggest objects ==="
arm-none-eabi-nm --size-sort -S "$ELFDIR/UOOMGUI.elf" | tail -12
echo
# Probe and variant builds go in a subdirectory so Output/ holds exactly one
# thing: the game.
# Same string the boot report shows and uoom.log carries, so "is this the build
# on my watch?" is a comparison rather than a guess.
BUILD_ID=$(git rev-parse --short=7 HEAD 2>/dev/null || echo nogit)
[ -n "$(git status --porcelain 2>/dev/null)" ] && BUILD_ID="$BUILD_ID+"

mkdir -p Output/probes
for f in Output/UOOM-*.uapp; do
    [ -e "$f" ] && mv "$f" Output/probes/
done
printf '\nbuild id %s -- the boot report and uoom.log show the same string\n' "$BUILD_ID"
ls -la Output/*.uapp 2>/dev/null

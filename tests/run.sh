#!/bin/sh
# Build and run the port-layer tests for every interesting configuration.
# No SDK, no toolchain, no watch required -- this is what keeps the two
# non-trivial layers of the port honest.
set -e

cd "$(dirname "$0")/.."
CC=${CC:-cc}
CFLAGS="-std=c99 -O1 -g -Wall -Wextra -Werror -ISoftware/Libs/Header"
OUT=tests/out
mkdir -p "$OUT"

run_cfg() {
    name=$1
    shift
    printf '\n=== %s ===\n' "$name"
    $CC $CFLAGS "$@" \
        tests/test_port.c \
        Software/Libs/Sources/uoom_video.c \
        Software/Libs/Sources/uoom_input.c \
        -o "$OUT/test_$name"
    "$OUT/test_$name"
}

run_cfg scaled_fill_dither
run_cfg scaled_fill_flat   -DUOOM_DITHER=0
run_cfg scaled_fit         -DUOOM_SCALE_MODE=1
run_cfg scaled_inscribed   -DUOOM_SCALE_MODE=2
run_cfg native             -DUOOM_RENDER_MODE=1

printf '\nall configurations passed\n'

# The sizes that actually govern the RAM budget are the ARM ones, not this
# machine's. Informational, but it is the number people get wrong.
if command -v clang >/dev/null 2>&1 && [ -f third_party/doomgeneric/doomgeneric/r_defs.h ]; then
    printf '\n=== structure sizes ===\n'
    tools/struct-sizes.sh 2>/dev/null | head -16 || true
fi

# The engine patches are the other thing that can rot silently -- upstream
# moves and a substitution stops matching. Re-applying from pristine is cheap
# and the script asserts every match count itself.
if [ -d third_party/doomgeneric/.git ]; then
    printf '\n=== engine patches re-apply cleanly ===\n'
    git -C third_party/doomgeneric checkout --quiet -- .
    python3 tools/apply-uoom-patches.py third_party/doomgeneric | tail -1
fi

# And if an IWAD is lying around, boot the real thing. This is the only test
# that exercises DOOM, the resample, the palette and the WAD layer together.
if [ -d third_party/doomgeneric/doomgeneric ] && \
   ls host/wad/*.wad >/dev/null 2>&1; then
    printf '\n=== end-to-end smoke run ===\n'
    make -C host >/dev/null
    host/out/uoom-host --wad host/wad --frames 300 \
        --keys "30:e,32:d,60:e,62:d,90:e,92:d,120:e,122:d,220:q,280:a" \
        | tail -3
else
    printf '\n(no IWAD in host/wad/ -- skipping the end-to-end run)\n'
fi

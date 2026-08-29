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

# The buffered FILE replacement the savegame path runs on. Compiled with
# UOOM_SMOKE_TEST so uoom_file.c leaves out its DOOM-facing half.
printf '\n=== buffered file ===\n'
$CC $CFLAGS -DUOOM_SMOKE_TEST=1 \
    tests/test_file.c Software/Libs/Sources/uoom_file.c \
    -o "$OUT/test_file"
"$OUT/test_file"

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
   ls wad/*.wad wad/*.WAD >/dev/null 2>&1; then
    printf '\n=== end-to-end smoke run ===\n'
    make -C host >/dev/null
    host/out/uoom-host --wad wad --frames 300 \
        --keys "30:e,32:d,60:e,62:d,90:e,92:d,120:e,122:d,220:q,280:a" \
        | tail -3
else
    printf '\n(no IWAD in wad/ -- skipping the end-to-end run)\n'
fi

# The no-WAD screen is the first thing a new user sees, and its QR code is the
# only way off it. "It looks like a QR code" is not a test, so decode the
# rendered framebuffer at native 240x240 and compare against the URL the
# generator baked in. This catches a regenerated symbol that no longer fits the
# round panel, and a layout change that clips a finder pattern.
printf '\n=== the no-WAD screen decodes ===\n'
PY=${PY:-.venv/bin/python3}
[ -x "$PY" ] || PY=python3
if ! "$PY" -c "import cv2" >/dev/null 2>&1; then
    printf 'skipped (no cv2: %s -m pip install opencv-python-headless)\n' "$PY"
elif [ ! -f host/out/uoom-host ]; then
    printf 'skipped (host harness not built)\n'
else
    QRDIR=$(mktemp -d)
    mkdir -p "$QRDIR/empty" "$QRDIR/out"
    host/out/uoom-host --wad "$QRDIR/empty" --frames 4 \
        --dump "$QRDIR/out" --every 3 >/dev/null 2>&1 || true
    "$PY" - "$QRDIR/out" <<'PYEOF'
import glob, re, sys, pathlib
import cv2, numpy as np

want = re.search(r'#define UOOM_QR_URL\s+"([^"]+)"',
                 pathlib.Path("Software/Libs/Header/uoom_qr.h").read_text()).group(1)

frames = sorted(glob.glob(sys.argv[1] + "/*.ppm"))
if not frames:
    sys.exit("the no-WAD screen rendered no frames")

img = cv2.imread(frames[-1])
got, _, _ = cv2.QRCodeDetector().detectAndDecode(img)

h, w = img.shape[:2]
if (h, w) != (240, 240):
    sys.exit(f"expected a 240x240 frame, got {w}x{h}")
if got != want:
    sys.exit(f"decoded {got!r}, expected {want!r}")
print(f"decoded at {w}x{h}: {got}")
PYEOF
    rm -rf "$QRDIR"
fi

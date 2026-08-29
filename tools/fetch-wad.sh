#!/bin/sh
# Put an IWAD in wad/, so the host harness has something to run.
#
# No IWAD is committed here, for the same reason doomgeneric is not: it is not
# ours. Freedoom is the default because it is BSD-licensed and settles the
# question; the shareware DOOM1.WAD is the other option and is what most of the
# measurements in docs/ were taken against.
#
#   tools/fetch-wad.sh              Freedoom Phase 1 (freedoom1.wad)
#   tools/fetch-wad.sh --shareware  id's shareware DOOM1.WAD
set -e

cd "$(dirname "$0")/.."
DEST=wad
mkdir -p "$DEST"

# Pinned, and checked. A WAD that silently differs from the one the numbers in
# docs/03-memory-budget.md came from would make every comparison a lie.
FREEDOOM_VER=0.13.0
FREEDOOM_URL="https://github.com/freedoom/freedoom/releases/download/v${FREEDOOM_VER}/freedoom-${FREEDOOM_VER}.zip"

# 4196020 bytes, 1264 lumps. This is shareware v1.9; v1.8 is the same size and
# lump count with MD5 5f4eb849b1af12887dec04a2a12e5e62, and both play here --
# the engine says which one it found ("This appears to be v1.8.") on startup.
SHAREWARE_URL="https://github.com/Akbar30Bill/DOOM_wads/raw/master/doom1.wad"
SHAREWARE_MD5=f0cefca49926d00903cf57551d901abe

md5_of() {
    if command -v md5 >/dev/null 2>&1; then
        md5 -q "$1"
    else
        md5sum "$1" | cut -d' ' -f1
    fi
}

if [ "$1" = "--shareware" ]; then
    OUT="$DEST/Doom1.WAD"
    if [ -f "$OUT" ]; then
        echo "already present: $OUT ($(md5_of "$OUT"))"
        exit 0
    fi
    echo "fetching shareware DOOM1.WAD..."
    curl -fsSL --retry 3 --retry-delay 2 --connect-timeout 20 \
        -o "$OUT.tmp" "$SHAREWARE_URL"
    got=$(md5_of "$OUT.tmp")
    if [ "$got" != "$SHAREWARE_MD5" ]; then
        rm -f "$OUT.tmp"
        echo "checksum mismatch: expected $SHAREWARE_MD5, got $got" >&2
        echo "The link points at a third-party re-host and may have changed." >&2
        echo "id's own copy is the DOS installer at" >&2
        echo "  https://www.gamers.org/pub/idgames/idstuff/doom/doom19s.zip" >&2
        echo "whose DOOMS_19.* files are DEICE-compressed and need extracting." >&2
        exit 1
    fi
    mv "$OUT.tmp" "$OUT"
    echo "$OUT ($got)"
    exit 0
fi

OUT="$DEST/freedoom1.wad"
if [ -f "$OUT" ]; then
    echo "already present: $OUT"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "fetching Freedoom $FREEDOOM_VER (24 MB, both phases)..."
curl -fsSL --retry 3 --retry-delay 2 --connect-timeout 20 \
    -o "$TMP/freedoom.zip" "$FREEDOOM_URL"
unzip -q -j "$TMP/freedoom.zip" "*/freedoom1.wad" -d "$DEST"
echo "$OUT"

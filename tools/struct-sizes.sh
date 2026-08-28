#!/bin/sh
# Report DOOM's structure sizes as they will be on the watch (32-bit ARM),
# not as they are on your laptop.
#
# This matters more than it sounds. Every pointer-heavy level structure is
# ~33% larger on a 64-bit host, so the host harness systematically overstates
# the zone requirement -- `line_t` is 88 bytes here and 72 there, `seg_t` 56
# and 32. Any RAM budget measured only on the host is wrong in the pessimistic
# direction, which is a bad way to decide what to optimise.
#
# No ARM toolchain needed: clang cross-compiles the probe with -fsyntax-only
# and we read the sizes out of a deliberate type error. Ugly, exact, portable.
set -e
cd "$(dirname "$0")/.."

DG=third_party/doomgeneric/doomgeneric
[ -f "$DG/r_defs.h" ] || { echo "run tools/fetch-doomgeneric.sh first" >&2; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/inc"

# Bare-metal clang has no sysroot here, so stub what DOOM's headers include
# and supply the fixed-width types ourselves.
: > "$TMP/inc/strings.h"
: > "$TMP/inc/string.h"
: > "$TMP/inc/stdio.h"
cat > "$TMP/inc/inttypes.h" <<'HDR'
typedef unsigned char      uint8_t;
typedef signed char        int8_t;
typedef unsigned short     uint16_t;
typedef signed short       int16_t;
typedef unsigned int       uint32_t;
typedef signed int         int32_t;
typedef unsigned long long uint64_t;
typedef signed long long   int64_t;
typedef unsigned int       size_t;
HDR

STRUCTS="seg_t line_t side_t sector_t node_t vertex_t subsector_t mobj_t
         visplane_t drawseg_t vissprite_t"

{
    echo '#include "doomtype.h"'
    echo '#include "r_defs.h"'
    echo '#include "p_mobj.h"'
    for s in $STRUCTS; do
        echo "char probe_$s[sizeof($s)];"
    done
    for s in $STRUCTS; do
        echo "int force_$s = probe_$s;"
    done
} > "$TMP/probe.c"

printf 'structure sizes on 32-bit ARM (target armv7m-none-eabi)\n\n'

clang -target armv7m-none-eabi -std=gnu99 -w -fsyntax-only -nostdlibinc \
      -I"$TMP/inc" -I"$DG" -ISoftware/Libs/Header "$TMP/probe.c" 2>&1 \
  | sed -n "s/.*int force_\([a-z_]*\) = .*/\1/p;s/.*type 'char\[\([0-9]*\)\]'.*/\1/p" \
  | paste - - \
  | awk '{ printf "  %-14s %4s bytes\n", $2, $1 }' || true

printf '\nfor comparison, this host:\n\n'
{
    echo '#include <stdio.h>'
    echo '#include "doomtype.h"'
    echo '#include "r_defs.h"'
    echo '#include "p_mobj.h"'
    echo 'int main(void) {'
    for s in $STRUCTS; do
        printf '  printf("  %%-14s %%4d bytes\\n", "%s", (int) sizeof(%s));\n' "$s" "$s"
    done
    echo '  return 0; }'
} > "$TMP/host.c"
cc -std=gnu99 -w -I"$DG" -ISoftware/Libs/Header "$TMP/host.c" -o "$TMP/host" \
  && "$TMP/host"

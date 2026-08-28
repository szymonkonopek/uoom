#!/bin/sh
# Find out how large an app image the watch's kernel will actually load.
#
# The SDK documents no maximum for UNA_APP_GUI_RAM_LENGTH, and it turns out to
# be a link-time ceiling rather than a reservation -- so the only real limit is
# whatever the loader can carve out, and that loader is not in the SDK. This
# builds the 13KB smoke app with N KB of static .bss bolted on and touched at
# startup, so a build that runs is proof the loader handed over that much.
#
#   tools/probe-loader.sh 1536          one size, GUI process
#   tools/probe-loader.sh 512 1024 1536
#   tools/probe-loader.sh --service 1024   weigh down the *service* instead
#
# The --service form answers a different and more useful question: whether the
# ceiling is per process. If it is, DOOM's zone can live in the service's .bss
# and the GUI can be handed the pointer -- there is no MMU, and the two regions
# share one address space. A service-ballast build writes uoom-svc.txt on the
# watch when it runs, which is how you know the service got its memory.
#
# Install them one at a time into Apps/UOOM/ (same APP_ID, so each replaces the
# last) and see which still draws its boot report. The largest that runs is the
# answer.
set -e
cd "$(dirname "$0")/.."

SERVICE=
if [ "$1" = "--service" ]; then
    SERVICE=1
    shift
fi

[ $# -gt 0 ] || { echo "usage: $0 [--service] <KB> [KB ...]" >&2; exit 1; }

for kb in "$@"; do
    if [ -n "$SERVICE" ]; then
        echo "=== service ballast ${kb}K ==="
        # clean each time: CMake caches UOOM_BALLAST_KB / APP_FILE_NAME, and a
        # reused build dir silently mislabels the artifact
        ./tools/build-watch.sh --smoke --svc-ballast="$kb" clean 2>&1 \
          | grep -E "Image|^Total|error|Error" | head -4
    else
        echo "=== GUI ballast ${kb}K ==="
        ./tools/build-watch.sh --smoke --ballast="$kb" clean 2>&1 \
          | grep -E "Image|^Total|error|Error" | head -4
    fi
done

echo
ls -la Output/UOOM-*ballast*.uapp 2>/dev/null

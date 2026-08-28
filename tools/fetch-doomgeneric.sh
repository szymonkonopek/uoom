#!/bin/sh
# Vendor the DOOM source into third_party/doomgeneric.
#
# doomgeneric is not committed here: it is 80-odd files of GPLv2 upstream we do
# not own. Our engine-level changes are applied by tools/apply-uoom-patches.py,
# which is the source of truth for them -- a script whose every substitution
# asserts its own match count, rather than diffs that rot silently when
# upstream moves. `tools/show-uoom-diff.sh` prints the resulting diff.
set -e

REPO=https://github.com/ozkl/doomgeneric.git
# Pinned so a fresh clone and a six-month-old clone build the same game.
COMMIT=dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284

cd "$(dirname "$0")/.."
DEST=third_party/doomgeneric

if [ -d "$DEST/.git" ]; then
    echo "already present: $DEST"
    git -C "$DEST" fetch --quiet origin "$COMMIT" 2>/dev/null || true
else
    mkdir -p third_party
    git clone --quiet "$REPO" "$DEST"
fi

git -C "$DEST" checkout --quiet "$COMMIT"
echo "doomgeneric at $(git -C "$DEST" rev-parse --short HEAD)"

# Start from pristine upstream every time, so re-running is idempotent.
git -C "$DEST" checkout --quiet -- .

python3 tools/apply-uoom-patches.py "$DEST" "$@"

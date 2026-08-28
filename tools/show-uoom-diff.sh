#!/bin/sh
# Print UOOM's engine-level changes as a diff against pristine doomgeneric.
# Useful for review, and for checking what an upstream bump broke.
set -e
cd "$(dirname "$0")/.."
git -C third_party/doomgeneric --no-pager diff --stat "$@"
echo
git -C third_party/doomgeneric --no-pager diff "$@"

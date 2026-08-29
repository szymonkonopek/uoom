#!/bin/sh
# Assemble Output/store/, the directory that becomes the store's ZIP.
#
# Docs/deploy.md in the SDK asks for the .uapp, app-manifest.json and icon.png;
# Docs/app-config-json.md adds previews/ and assets/{icons,previews}/. This
# builds all of it from what is already in the repository, so a version bump
# does not mean hand-editing a version string into two places and getting one
# of them wrong.
#
# The listing text lives in Resources/store/description.txt -- prose belongs in
# a file you can read, not inside a JSON string.
set -e

cd "$(dirname "$0")/.."

UAPP=$(ls -t Output/UOOM_*.uapp 2>/dev/null | head -1)
if [ -z "$UAPP" ]; then
    echo "no Output/UOOM_*.uapp -- run tools/build-watch.sh first" >&2
    exit 1
fi
BIN=$(basename "$UAPP")
VERSION=$(echo "$BIN" | sed 's/^UOOM_//; s/\.uapp$//')

# The id the packer actually wrote into the image, not the one we hope is there.
APP_ID=$(python3 - "$UAPP" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read(16)
print(f"{struct.unpack('<QII', d)[0]:016X}")
PY
)

OUT=Output/store
rm -rf "$OUT"
mkdir -p "$OUT/assets/icons" "$OUT/assets/previews" "$OUT/previews"

cp "$UAPP"                    "$OUT/"
cp Resources/icon_60x60.png   "$OUT/icon.png"
cp Resources/icon_60x60.png   "$OUT/assets/icons/"
cp Resources/icon_30x30.png   "$OUT/assets/icons/"

# Order is the order the store shows them, so the game leads and the
# housekeeping screen comes last.
i=1
for n in e1m2 e1m1-combat e1m1-corridor title menu intermission no-assets; do
    src=docs/img/shot-$n.png
    [ -f "$src" ] || { echo "missing $src" >&2; exit 1; }
    name=$(printf '%02d-%s.png' "$i" "$n")
    cp "$src" "$OUT/assets/previews/$name"
    cp "$src" "$OUT/previews/$name"
    i=$((i + 1))
done

python3 - "$OUT" "$BIN" "$VERSION" "$APP_ID" <<'PY'
import json, pathlib, sys

out, binary, version, app_id = sys.argv[1:5]
desc = pathlib.Path("Resources/store/description.txt").read_text().strip()

pathlib.Path(out, "app-manifest.json").write_text(json.dumps({
    "manifest_version":  1,
    "type":              ["utility"],
    "name":              "UOOM",
    "id":                app_id,
    "appVersion":        version,
    # Raised to the ABI floor below by the SDK's own resolver; never hand-set.
    "minKernelVersion":  "0.0.0",
    "binary":            binary,
    "icon":              "assets/icons/icon_60x60.png",
    "previews":          "assets/previews/",
    "requiredHardware":  [],
    "description":       desc,
}, indent=2) + "\n")
PY

# The floor is a property of the SDK we linked against, so ask the SDK.
STAMP=$UNA_SDK/Utilities/Scripts/app_packer/min_kernel_version.py
[ -f "$STAMP" ] || STAMP=../una-sdk/Utilities/Scripts/app_packer/min_kernel_version.py
if [ -f "$STAMP" ]; then
    python3 "$STAMP" --stamp "$OUT/app-manifest.json" >/dev/null
    python3 "$STAMP" --check "$OUT/app-manifest.json"
else
    echo "warning: no SDK resolver found; minKernelVersion left at 0.0.0" >&2
fi

echo
echo "$OUT ready -- $BIN, version $VERSION, id $APP_ID"
echo "zip it with:  (cd $OUT && zip -r ../UOOM_${VERSION}_store.zip .)"

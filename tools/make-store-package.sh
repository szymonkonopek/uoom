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

# The store icon is a different thing from the watch icon and comes from a
# different source. Resources/icon_{30,60}.png are baked into the .uapp and are
# quantised for the panel's ABGR2222 -- deliberately crude, and wrong to show a
# phone. This one is full colour at 512x512, straight from the artwork.
python3 - "$OUT" <<'PY'
import pathlib, sys
from PIL import Image

out = pathlib.Path(sys.argv[1])
src = Image.open("Resources/src/app-icon.jpeg").convert("RGB")
icon = src.resize((512, 512), Image.LANCZOS)
icon.save(out / "icon.png")
PY

# The watch's own icons travel along under assets/, where the manifest format
# expects application and widget icons, but they are not what the listing shows.
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

# Exactly the key set of Docs/Tutorials/Files/Output/app-manifest.json in the
# SDK -- the one manifest it ships as the output of its own tutorial, and the
# only shape the store's validator actually accepts.
#
# The docs disagree with it in two directions and both were rejected on upload:
# the Waypoint example carries `description` and no supports* fields, and the
# annotated example in app-config-json.md adds `previews` on top. The live
# schema requires every supports* key even for a utility, and refuses anything
# beyond the set below ("must NOT have additional properties"). So the listing
# prose does not travel in the package at all -- Resources/store/description.txt
# is there to paste into the store's own form.
pathlib.Path(out, "app-manifest.json").write_text(json.dumps({
    "manifest_version":   1,
    "type":               ["utility"],
    "name":               "UOOM",
    "icon":               "icon.png",
    "binary":             binary,
    "appVersion":         version,
    # Raised to the ABI floor below by the SDK's own resolver; never hand-set.
    "minKernelVersion":   "0.0.0",
    "requiredHardware":   [],
    "stravaExport":       False,
    "id":                 app_id,
    "supportsLaps":       False,
    "supportsDistance":   False,
    "supportsTrack":      False,
    "supportsHeartbeat":  False,
    "supportsElevation":  False,
    "supportsStep":       False,
    "supportsSpeed":      "none",
    "customMeasures":     [],
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

# -X drops the extra attributes, and the exclusions keep macOS's .DS_Store and
# resource forks out -- an upload is not the place to discover them.
ZIP=Output/UOOM_${VERSION}_store.zip
rm -f "$ZIP"
(cd "$OUT" && zip -qr -X "../$(basename "$ZIP")" . -x '.*' '*/.*' '__MACOSX/*')

echo
echo "$ZIP -- $BIN, version $VERSION, id $APP_ID"
ls -la "$ZIP"

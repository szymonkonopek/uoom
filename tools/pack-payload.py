#!/usr/bin/env python3
"""Carry a data file inside a .uapp package.

The watch keeps the installed package in the app's own directory, and the app
can open it -- both established by the smoke probe. So a WAD appended to the
package is readable from storage at no RAM cost, and ships as one file instead
of two.

The container makes room for a passenger, up to a point. From the SDK's
app_merging.py the layout is:

    [header 48B][icon60][icon30][service][gui][crc32 of everything above]

The CRC is the last four bytes and covers the rest, so a payload placed just
before it still verifies. The header carries `service_size`, which is how the
loader finds the GUI blob -- but there is *no* gui_size field, so the GUI's
length can only be "the rest of the file". Whether that matters depends on
whether the loader trusts it or reads the inner UAPP header's own per-section
sizes. This tool exists to find out; see docs/09-shipping-the-wad.md.

The payload is followed by a 16-byte footer, immediately before the CRC:

    magic   char[8]   "UOOMWAD1"
    offset  uint32    payload offset from the start of the file
    length  uint32    payload length

so the app can find it by reading twenty bytes from the end.

    tools/pack-payload.py Output/UOOM_0.0.0-dev.uapp wad/DOOM1.WAD \\
        -o Output/UOOM-bundled_0.0.0-dev.uapp
"""

import argparse
import struct
import sys
import zlib
from pathlib import Path

MAGIC = b"UOOMWAD1"
FOOTER = struct.Struct("<8sII")          # magic, offset, length
CRC_LEN = 4


def read_footer(data: bytes):
    """Return (offset, length) if `data` already carries a payload."""
    if len(data) < FOOTER.size + CRC_LEN:
        return None
    magic, offset, length = FOOTER.unpack_from(data, len(data) - CRC_LEN - FOOTER.size)
    if magic != MAGIC:
        return None
    return offset, length


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("uapp", type=Path, help="the package to add to")
    ap.add_argument("payload", type=Path, nargs="?", help="the file to carry")
    ap.add_argument("-o", "--output", type=Path, help="where to write it")
    ap.add_argument("--check", action="store_true",
                    help="report what the package already carries and stop")
    args = ap.parse_args()

    data = args.uapp.read_bytes()

    if len(data) < CRC_LEN:
        print(f"{args.uapp}: too short to be a package", file=sys.stderr)
        return 1

    body, crc_bytes = data[:-CRC_LEN], data[-CRC_LEN:]
    stored = struct.unpack("<I", crc_bytes)[0]
    actual = zlib.crc32(body) & 0xFFFFFFFF

    if stored != actual:
        print(f"{args.uapp}: CRC mismatch -- stored {stored:#010x}, "
              f"computed {actual:#010x}. Not a .uapp, or already damaged.",
              file=sys.stderr)
        return 1

    existing = read_footer(data)

    if args.check or args.payload is None:
        if existing is None:
            print(f"{args.uapp}: {len(data)} bytes, no payload")
        else:
            offset, length = existing
            print(f"{args.uapp}: {len(data)} bytes, "
                  f"payload of {length} at offset {offset}")
        return 0

    if existing is not None:
        # Strip the old passenger rather than stacking a second one.
        offset, length = existing
        body = body[:offset]
        print(f"replacing the existing payload of {length} bytes")

    payload = args.payload.read_bytes()
    offset = len(body)

    blob = body + payload + FOOTER.pack(MAGIC, offset, len(payload))
    out = blob + struct.pack("<I", zlib.crc32(blob) & 0xFFFFFFFF)

    dest = args.output or args.uapp
    dest.write_bytes(out)

    print(f"{dest}: {len(out)} bytes "
          f"({len(body)} package + {len(payload)} payload + "
          f"{FOOTER.size} footer + {CRC_LEN} crc)")
    print(f"payload at offset {offset}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

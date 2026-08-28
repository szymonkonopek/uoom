#!/usr/bin/env python3
"""Convert the host harness's PPM frame dumps to PNG, optionally scaled.

Pure stdlib -- the whole point of this project is that you can inspect what it
renders without installing anything.

    tools/ppm2png.py host/out/frames/*.ppm --scale 2
"""
import struct
import sys
import zlib
from pathlib import Path


def read_ppm(path):
    data = path.read_bytes()
    # header: P6 <w> <h> <max>, whitespace-separated, then binary
    fields = []
    i = 0
    while len(fields) < 4:
        while data[i : i + 1].isspace():
            i += 1
        if data[i : i + 1] == b"#":
            while data[i : i + 1] not in (b"\n", b""):
                i += 1
            continue
        j = i
        while not data[j : j + 1].isspace():
            j += 1
        fields.append(data[i:j])
        i = j
    i += 1
    w, h = int(fields[1]), int(fields[2])
    return w, h, data[i : i + w * h * 3]


def write_png(path, w, h, rgb, scale=1):
    raw = bytearray()
    for y in range(h):
        row = rgb[y * w * 3 : (y + 1) * w * 3]
        if scale != 1:
            wide = bytearray()
            for x in range(w):
                wide.extend(row[x * 3 : x * 3 + 3] * scale)
            row = bytes(wide)
        for _ in range(scale):
            raw.append(0)
            raw.extend(row)

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR",
                   struct.pack(">IIBBBBB", w * scale, h * scale, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(bytes(raw), 6))
           + chunk(b"IEND", b""))
    path.write_bytes(png)
    return len(png)


def main():
    args = [a for a in sys.argv[1:]]
    scale = 1
    if "--scale" in args:
        k = args.index("--scale")
        scale = int(args[k + 1])
        del args[k : k + 2]
    if not args:
        sys.exit(__doc__)
    for a in args:
        src = Path(a)
        dst = src.with_suffix(".png")
        w, h, rgb = read_ppm(src)
        n = write_png(dst, w, h, rgb, scale)
        print(f"{dst}  {w * scale}x{h * scale}  {n} bytes")


if __name__ == "__main__":
    main()

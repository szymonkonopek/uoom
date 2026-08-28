#!/usr/bin/env python3
"""Generate Resources/icon_30x30.png and icon_60x60.png.

The SDK requires both sizes for every app. Written as a generator rather than
committed binaries so the icon is diffable and so both sizes stay in sync.

A reticle rather than a skull: at 30x30 a skull turns to mud, while a ring with
crosshair gaps still reads at a glance on a watch launcher. Pure stdlib -- no
Pillow, no build dependency.
"""
import struct
import zlib
from pathlib import Path

BG = (14, 10, 10)        # near-black, warm
RING = (176, 26, 22)     # DOOM red
INNER = (232, 196, 120)  # brass, for the centre dot


def shade(px, py, size):
    """Return the colour of pixel (px, py), supersampled 3x3 for smooth edges."""
    acc = [0.0, 0.0, 0.0]
    samples = 0
    c = (size - 1) / 2.0
    r_outer = size * 0.44
    r_inner = size * 0.30
    dot = size * 0.075
    arm = size * 0.085          # half-width of the crosshair gap arms

    for sy in range(3):
        for sx in range(3):
            x = px + (sx + 0.5) / 3.0 - c - 0.5
            y = py + (sy + 0.5) / 3.0 - c - 0.5
            d = (x * x + y * y) ** 0.5

            col = BG
            if d <= dot:
                col = INNER
            elif r_inner <= d <= r_outer:
                # break the ring at the four axes so it reads as a reticle
                if not (abs(x) < arm or abs(y) < arm):
                    col = RING
            elif d < r_inner:
                # crosshair arms reaching in toward the centre
                if (abs(x) < arm * 0.55 or abs(y) < arm * 0.55) and d > dot * 1.6:
                    col = RING

            for i in range(3):
                acc[i] += col[i]
            samples += 1

    return tuple(int(v / samples + 0.5) for v in acc)


def write_png(path, size):
    raw = bytearray()
    for y in range(size):
        raw.append(0)                      # filter type 0
        for x in range(size):
            raw.extend(shade(x, y, size))

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
           + chunk(b"IEND", b""))
    path.write_bytes(png)
    print(f"{path}  {size}x{size}  {len(png)} bytes")


if __name__ == "__main__":
    out = Path(__file__).resolve().parent.parent / "Resources"
    out.mkdir(exist_ok=True)
    write_png(out / "icon_30x30.png", 30)
    write_png(out / "icon_60x60.png", 60)

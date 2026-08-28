#!/usr/bin/env python3
"""Build Resources/icon_30x30.png and icon_60x60.png from the source art.

Four decisions, each made by looking at the result at 8x rather than by
reasoning about it:

**Quantise here, not in the packer.** The SDK requires both sizes, and
app_merging.py converts them to the panel's ABGR2222 by *truncating* each
channel to its top two bits. For a dark metallic wordmark that is unkind:
150,120,90 truncates to 170,85,85, visibly red. Doing it here leaves every
channel an exact multiple of 85, and the packer's truncation of those is
lossless (85 is 0b01010101, whose top two bits are 0b01), so what ships is what
this tool chose. Same trick as the game's palette collapse in uoom_video.c.

**No dithering.** The 2x2 ordered dither that rescues DOOM's light ramps is
wrong at 30 pixels: each pixel is a large fraction of a letter, so the dither
scatters the letter shapes into speckle instead of smoothing a gradient.
Rounding keeps the forms.

**Autocrop, then fit the whole wordmark.** The source has the logo on a wide
black field; scaling the square as-is spends 40% of the icon on nothing. And
the *whole* wordmark, not a two-letter crop that fills the square better -- at
30x30 the crop reads as coloured texture while the full mark keeps the
recognisable jagged silhouette and four-letter rhythm.

**A contrast and saturation boost before quantising.** Four levels per channel
turn the dark circuitry inside the letters to mud, which fights the silhouette.
Pushing contrast first makes the glowing edges land on distinct levels; at 60px
the difference is between four visible letters and a smear.

Source lives in Resources/src/ so the icons are reproducible from the repo. If
it is missing, a generated reticle stands in, because the build cannot proceed
without both files.

    tools/make_icons.py
"""
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RES = ROOT / "Resources"
SRC = RES / "src"

LEVELS = 4
STEP = 255 // (LEVELS - 1)          # 85

# Tuned by eye at 8x; see the module docstring.
CONTRAST = 1.35
SATURATION = 1.25

# Anything above this in the source is the logo rather than the black field.
CROP_THRESHOLD = 18


def quantise(rgb):
    """Snap to the panel's 64 colours, rounding rather than dithering."""
    out = bytearray(len(rgb))
    for i in range(0, len(rgb), 3):
        for c in range(3):
            q = (rgb[i + c] * (LEVELS - 1) + 127) // 255
            out[i + c] = min(q, LEVELS - 1) * STEP
    return bytes(out)


def write_png(path, w, h, rgb):
    raw = bytearray()
    for y in range(h):
        raw.append(0)                       # filter type 0
        raw.extend(rgb[y * w * 3:(y + 1) * w * 3])

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
           + chunk(b"IEND", b""))
    path.write_bytes(png)
    return len(png)


def from_source(src, size):
    from PIL import Image, ImageEnhance

    im = Image.open(src).convert("RGB")

    # Trim the black field so the icon spends its pixels on the wordmark.
    box = im.convert("L").point(lambda v: 255 if v > CROP_THRESHOLD else 0).getbbox()
    if box is not None:
        im = im.crop(box)

    im = ImageEnhance.Contrast(im).enhance(CONTRAST)
    im = ImageEnhance.Color(im).enhance(SATURATION)

    # Fit, centred, on black. LANCZOS before quantising -- resampling after
    # would average the four levels back into values the panel cannot show.
    w, h = im.size
    scale = min(size / w, size / h)
    nw, nh = max(1, round(w * scale)), max(1, round(h * scale))
    canvas = Image.new("RGB", (size, size), (0, 0, 0))
    canvas.paste(im.resize((nw, nh), Image.LANCZOS), ((size - nw) // 2, (size - nh) // 2))
    return canvas.tobytes()


def reticle(size):
    """Fallback: a crosshair, which still reads at 30px where a skull does not."""
    bg, ring, dot = (14, 10, 10), (176, 26, 22), (232, 196, 120)
    out = bytearray()
    c = (size - 1) / 2.0
    r_out, r_in, r_dot, arm = size * 0.44, size * 0.30, size * 0.075, size * 0.085
    for py in range(size):
        for px in range(size):
            acc = [0.0, 0.0, 0.0]
            for sy in range(3):
                for sx in range(3):
                    x = px + (sx + 0.5) / 3.0 - c - 0.5
                    y = py + (sy + 0.5) / 3.0 - c - 0.5
                    d = (x * x + y * y) ** 0.5
                    col = bg
                    if d <= r_dot:
                        col = dot
                    elif r_in <= d <= r_out and not (abs(x) < arm or abs(y) < arm):
                        col = ring
                    elif d < r_in and (abs(x) < arm * 0.55 or abs(y) < arm * 0.55) \
                            and d > r_dot * 1.6:
                        col = ring
                    for i in range(3):
                        acc[i] += col[i]
            out.extend(int(v / 9 + 0.5) for v in acc)
    return bytes(out)


def main():
    RES.mkdir(exist_ok=True)
    for size in (30, 60):
        src = None
        for stem in ("uoom-logo", f"icon{size}"):
            for ext in ("png", "jpg", "jpeg"):
                p = SRC / f"{stem}.{ext}"
                if p.exists():
                    src = p
                    break
            if src is not None:
                break
        if src is not None:
            try:
                rgb = from_source(src, size)
                origin = src.name
            except ImportError:
                print("Pillow not available; using the generated reticle instead",
                      file=sys.stderr)
                rgb, origin = reticle(size), "generated"
        else:
            rgb, origin = reticle(size), "generated"

        rgb = quantise(rgb)
        out = RES / f"icon_{size}x{size}.png"
        n = write_png(out, size, size, rgb)
        print(f"{out.name}  {size}x{size}  {n} bytes  <- {origin}")


if __name__ == "__main__":
    main()

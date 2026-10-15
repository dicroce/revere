#!/usr/bin/env python3
"""Generate the icons build-icons.sh doesn't: Revere's embedded title-bar icon and
all of Vision's icons, in the same style.

The style is build-icons.sh's desktop tile: a square, full-bleed tile with the mark
(revere-mark-compact.svg: outline 12->52 with a 3.5 stroke, stamp 27->37 on the 64
grid) spanning half the tile. 16/32/48 are drawn on the pixel grid with the same
coordinates build-icons.sh uses; 256 is rendered from the geometry. Revere is the
white mark on ink; Vision is the inverse (ink mark on paper) so the two apps are
told apart in the taskbar.

Pure Python (no rsvg/ImageMagick needed). Writes:
  apps/revere/include/R_32x32.h      Revere window (title bar) icon
  apps/vision/V.ico                  Vision exe and window icon: 16, 32, 48, 256
  apps/vision/V.png                  256: Linux install icon
  apps/vision/include/V_32x32.h      Vision window (title bar) icon

Run from anywhere: python tools/icons/make_app_icons.py
"""

import os
import struct
import zlib

INK = (0x18, 0x18, 0x1B)
PAPER = (0xFF, 0xFF, 0xFF)

# size -> (outline first px, outline last px, stroke px, stamp first px, stamp last px),
# inclusive pixel coordinates, identical to build-icons.sh's pixel_tile calls.
PIXEL_TILES = {
    16: (4, 11, 1, 7, 8),
    32: (8, 23, 2, 14, 17),
    48: (12, 35, 2, 21, 26),
}
SMOOTH_SIZE = 256
SS = 8  # supersamples per axis for the smooth size

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))


def pixel_tile(size, tile, mark):
    a, b, w, c, d = PIXEL_TILES[size]
    rows = []
    for y in range(size):
        row = bytearray()
        for x in range(size):
            in_outline = a <= x <= b and a <= y <= b and not (a + w <= x <= b - w and a + w <= y <= b - w)
            in_stamp = c <= x <= d and c <= y <= d
            rgb = mark if (in_outline or in_stamp) else tile
            row += bytes((*rgb, 255))
        rows.append(bytes(row))
    return rows


def smooth_tile(size, tile, mark):
    # build-icons.sh: the outline's outer edge (10.25 -> 53.75, 43.5 units) spans half the tile.
    s = 0.5 * size / 43.5
    t = size / 2.0 - 32.0 * s

    def g(v):  # 64-grid unit -> pixels
        return t + v * s

    o0, o1 = g(10.25), g(53.75)
    i0, i1 = g(13.75), g(50.25)
    s0, s1 = g(27.0), g(37.0)
    rows = []
    for py in range(size):
        row = bytearray()
        for px in range(size):
            hits = 0
            for sy in range(SS):
                y = py + (sy + 0.5) / SS
                for sx in range(SS):
                    x = px + (sx + 0.5) / SS
                    in_outline = o0 <= x <= o1 and o0 <= y <= o1 and not (i0 <= x <= i1 and i0 <= y <= i1)
                    if in_outline or (s0 <= x <= s1 and s0 <= y <= s1):
                        hits += 1
            m = hits / (SS * SS)
            row += bytes((*(round(tile[i] * (1 - m) + mark[i] * m) for i in range(3)), 255))
        rows.append(bytes(row))
    return rows


def png_bytes(size, rows):
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + r for r in rows)
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def dib_bytes(size, rows):
    """Classic 32bpp BGRA DIB icon image (bottom-up) with its AND mask."""
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    xor = bytearray()
    for r in reversed(rows):
        for i in range(0, len(r), 4):
            xor += bytes((r[i + 2], r[i + 1], r[i], r[i + 3]))
    stride = ((size + 31) // 32) * 4
    return header + bytes(xor) + bytes(stride * size)  # opaque tile: AND mask all zero


def ico_bytes(images):
    """images: list of (size, rows). 256 is stored as PNG, smaller sizes as DIB."""
    blobs = [png_bytes(s, r) if s >= 256 else dib_bytes(s, r) for s, r in images]
    out = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for (s, _), blob in zip(images, blobs):
        dim = 0 if s >= 256 else s
        out += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    return out + b"".join(blobs)


def c_array(name, data):
    lines = ["unsigned char %s[] = {" % name]
    for i in range(0, len(data), 12):
        chunk = ", ".join("0x%02x" % b for b in data[i:i + 12])
        lines.append("  " + chunk + ("," if i + 12 < len(data) else ""))
    lines.append("};")
    lines.append("unsigned int %s_len = %d;" % (name, len(data)))
    return "\n".join(lines) + "\n"


def write_header(path, name, size, tile, mark):
    with open(path, "w", newline="\n") as f:
        f.write(c_array(name, png_bytes(size, pixel_tile(size, tile, mark))))
    print("wrote", os.path.relpath(path, ROOT))


if __name__ == "__main__":
    write_header(os.path.join(ROOT, "apps/revere/include/R_32x32.h"), "R_32x32_png", 32, INK, PAPER)

    images = [(s, pixel_tile(s, PAPER, INK)) for s in sorted(PIXEL_TILES)]
    images.append((SMOOTH_SIZE, smooth_tile(SMOOTH_SIZE, PAPER, INK)))
    with open(os.path.join(ROOT, "apps/vision/V.ico"), "wb") as f:
        f.write(ico_bytes(images))
    with open(os.path.join(ROOT, "apps/vision/V.png"), "wb") as f:
        f.write(png_bytes(SMOOTH_SIZE, images[-1][1]))
    print("wrote apps/vision/V.ico, apps/vision/V.png")
    write_header(os.path.join(ROOT, "apps/vision/include/V_32x32.h"), "V_32x32_png", 32, PAPER, INK)

#!/usr/bin/env python3
"""Generate installer/softvoice.ico with no image library.

A speaker cone with three sound arcs, drawn straight into BGRA pixels and
wrapped in the ICO/BMP containers Windows expects. Kept as source rather than
as a committed binary so the icon can be regenerated or recoloured without a
paint program.
"""
import math
import os
import struct
import sys

SIZES = (16, 24, 32, 48, 64, 256)

# A blue that stays legible on both the light and the dark taskbar.
BG = (0xC8, 0x6B, 0x1F)      # BGR
FG = (0xFF, 0xFF, 0xFF)
ACCENT = (0xE8, 0xC9, 0x8A)


def draw(size):
    """Return `size` rows of BGRA, top row first."""
    px = [[(0, 0, 0, 0)] * size for _ in range(size)]
    c = size / 2.0
    radius = size * 0.48
    corner = size * 0.22

    def put(x, y, colour, alpha=255):
        if 0 <= x < size and 0 <= y < size:
            px[y][x] = (colour[0], colour[1], colour[2], alpha)

    # Rounded-square background.
    for y in range(size):
        for x in range(size):
            dx = abs(x + 0.5 - c)
            dy = abs(y + 0.5 - c)
            inner = radius - corner
            if dx <= inner or dy <= inner:
                inside = dx <= radius and dy <= radius
            else:
                inside = math.hypot(dx - inner, dy - inner) <= corner
            if inside:
                put(x, y, BG)

    # Speaker: a box on the left, a cone opening to the right.
    box_l = c - size * 0.30
    box_r = c - size * 0.12
    box_t = c - size * 0.10
    box_b = c + size * 0.10
    cone_r = c + size * 0.02
    cone_h = size * 0.26

    for y in range(size):
        for x in range(size):
            fx = x + 0.5
            fy = y + 0.5
            if box_l <= fx <= box_r and box_t <= fy <= box_b:
                put(x, y, FG)
            elif box_r <= fx <= cone_r:
                # The cone widens linearly from the box to its mouth.
                t = (fx - box_r) / max(cone_r - box_r, 1e-6)
                half = (box_b - box_t) / 2 + t * (cone_h - (box_b - box_t) / 2)
                if abs(fy - c) <= half:
                    put(x, y, FG)

    # Three arcs to the right of the cone, the sound coming out.
    for n, factor in enumerate((0.14, 0.24, 0.34)):
        arc_r = size * factor + size * 0.06
        thickness = max(size * 0.045, 0.9)
        colour = FG if n == 0 else ACCENT
        steps = max(int(arc_r * 12), 64)
        for i in range(steps):
            angle = -0.75 + 1.5 * i / (steps - 1)
            for t in range(int(thickness * 2) + 1):
                rr = arc_r + t / 2.0
                x = int(cone_r + math.cos(angle) * rr - size * 0.02)
                y = int(c + math.sin(angle) * rr)
                put(x, y, colour)
    return px


def bmp_payload(px, size):
    """A DIB with a doubled height and an AND mask, as ICO requires."""
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0,
                         size * size * 4, 2835, 2835, 0, 0)
    body = bytearray()
    for y in reversed(range(size)):        # BMP rows run bottom-up
        for x in range(size):
            b, g, r, a = px[y][x]
            body += bytes((b, g, r, a))
    # 1bpp AND mask, rows padded to 4 bytes. Fully zero: alpha does the work.
    stride = ((size + 31) // 32) * 4
    body += bytes(stride * size)
    return header + bytes(body)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = sys.argv[1] if len(sys.argv) > 1 \
        else os.path.join(root, "installer", "softvoice.ico")
    os.makedirs(os.path.dirname(out), exist_ok=True)

    images = [(s, bmp_payload(draw(s), s)) for s in SIZES]

    offset = 6 + 16 * len(images)
    entries = bytearray()
    blobs = bytearray()
    for size, blob in images:
        entries += struct.pack("<BBBBHHII", size & 0xFF, size & 0xFF, 0, 0, 1,
                               32, len(blob), offset)
        blobs += blob
        offset += len(blob)

    with open(out, "wb") as f:
        f.write(struct.pack("<HHH", 0, 1, len(images)))
        f.write(entries)
        f.write(blobs)
    print("wrote %s (%d bytes, %d sizes)"
          % (out, os.path.getsize(out), len(images)))


if __name__ == "__main__":
    main()

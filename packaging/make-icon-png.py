#!/usr/bin/env python3
# Draws packaging/icon.png, the application icon.
#
#   python3 packaging/make-icon-png.py packaging/icon.png
#
# The picture is a chessboard with a pawn standing on it. Every shape is
# drawn from the numbers below, so the icon is original work. Nothing here
# comes from the game CD. Run this script again after changing a colour or a
# shape, then commit the new icon.png.
import struct
import sys
import zlib

SIZE = 1024
SAMPLES = 3  # Draw at three times the size, then average, so edges look smooth.

BACKGROUND = (18, 26, 43)
LIGHT_SQUARE = (226, 220, 201)
DARK_SQUARE = (58, 92, 112)
PAWN = (232, 196, 92)
PAWN_EDGE = (26, 34, 52)


def rounded_rect(x, y, left, top, right, bottom, radius):
    """Says whether the point is inside a rectangle with rounded corners."""
    if x < left or x > right or y < top or y > bottom:
        return False
    cx = min(max(x, left + radius), right - radius)
    cy = min(max(y, top + radius), bottom - radius)
    return (x - cx) ** 2 + (y - cy) ** 2 <= radius * radius


def disc(x, y, cx, cy, radius):
    return (x - cx) ** 2 + (y - cy) ** 2 <= radius * radius


def pawn(x, y, grow):
    """Says whether the point is inside the pawn, widened by grow pixels."""
    if disc(x, y, 512, 372, 96 + grow):
        return True
    # The collar under the head.
    if 440 - grow <= y <= 486 + grow and abs(x - 512) <= 118 + grow:
        return True
    # The body tapers from the collar down to the foot.
    if 486 - grow <= y <= 700 + grow:
        share = (y - 486) / 214.0
        half = 78 + 46 * share * share
        if abs(x - 512) <= half + grow:
            return True
    # The base the pawn stands on.
    if 700 - grow <= y <= 772 + grow:
        share = (y - 700) / 72.0
        half = 138 + 74 * share
        if abs(x - 512) <= half + grow:
            return True
    return False


def colour_at(x, y):
    if not rounded_rect(x, y, 32, 32, 992, 992, 176):
        return None
    if not rounded_rect(x, y, 128, 128, 896, 896, 56):
        return BACKGROUND
    if pawn(x, y, 0):
        return PAWN
    if pawn(x, y, 14):
        return PAWN_EDGE
    column = int((x - 128) // 96)
    row = int((y - 128) // 96)
    return LIGHT_SQUARE if (column + row) % 2 == 0 else DARK_SQUARE


def main(path):
    rows = []
    step = 1.0 / SAMPLES
    for pixel_y in range(SIZE):
        row = bytearray([0])
        for pixel_x in range(SIZE):
            red = green = blue = alpha = 0
            for sub_y in range(SAMPLES):
                y = pixel_y + (sub_y + 0.5) * step
                for sub_x in range(SAMPLES):
                    x = pixel_x + (sub_x + 0.5) * step
                    found = colour_at(x, y)
                    if found is None:
                        continue
                    red += found[0]
                    green += found[1]
                    blue += found[2]
                    alpha += 255
            total = SAMPLES * SAMPLES
            covered = alpha // 255
            if covered == 0:
                row += bytes([0, 0, 0, 0])
            else:
                row += bytes([red // covered, green // covered, blue // covered,
                              alpha // total])
        rows.append(bytes(row))
    raw = b"".join(rows)

    def chunk(tag, body):
        head = tag + body
        return struct.pack(">I", len(body)) + head + struct.pack(">I", zlib.crc32(head))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as handle:
        handle.write(png)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "packaging/icon.png")

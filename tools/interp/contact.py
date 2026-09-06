"""Draw a contact sheet from an interp output directory so a person can look.

Every Nth frame becomes one cell. Cells sit over a grey checkerboard, so a hole
in the alpha shows as squares and a smeared edge shows as haze. The sheet writes
one opaque RGB PNG.
"""

import argparse
import json
import os

from . import png

CHECK_LIGHT = 0xB4
CHECK_DARK = 0x78
CHECK_SIZE = 8
GUTTER = 2
GUTTER_COLOUR = 0x28


def shrink(pixels, width, height, factor):
    """Box average an RGBA buffer down by an integer factor."""
    if factor == 1:
        return pixels, width, height
    out_w = width // factor
    out_h = height // factor
    out = bytearray(out_w * out_h * 4)
    area = factor * factor
    for y in range(out_h):
        for x in range(out_w):
            sums = [0, 0, 0, 0]
            for dy in range(factor):
                base = ((y * factor + dy) * width + x * factor) * 4
                for dx in range(factor):
                    off = base + dx * 4
                    sums[0] += pixels[off]
                    sums[1] += pixels[off + 1]
                    sums[2] += pixels[off + 2]
                    sums[3] += pixels[off + 3]
            dst = (y * out_w + x) * 4
            for c in range(4):
                out[dst + c] = sums[c] // area
    return out, out_w, out_h


def sheet(out_dir, every=8, columns=10, factor=2, out_path=None):
    """Write the contact sheet and return its path."""
    with open(os.path.join(out_dir, "manifest.json")) as fh:
        manifest = json.load(fh)
    frames = manifest["frames"][::every]
    cells = []
    cell_w = cell_h = 0
    for frame in frames:
        w, h, channels, pixels = png.read(os.path.join(out_dir, frame["file"]))
        if channels != 4:
            raise ValueError("frame %s is not RGBA" % frame["file"])
        pixels, w, h = shrink(pixels, w, h, factor)
        cells.append(pixels)
        cell_w, cell_h = w, h

    rows_count = (len(cells) + columns - 1) // columns
    sheet_w = columns * (cell_w + GUTTER) + GUTTER
    sheet_h = rows_count * (cell_h + GUTTER) + GUTTER
    canvas = bytearray([GUTTER_COLOUR]) * (sheet_w * sheet_h * 3)

    for i, cell in enumerate(cells):
        cx = GUTTER + (i % columns) * (cell_w + GUTTER)
        cy = GUTTER + (i // columns) * (cell_h + GUTTER)
        for y in range(cell_h):
            dst = ((cy + y) * sheet_w + cx) * 3
            for x in range(cell_w):
                src = (y * cell_w + x) * 4
                a = cell[src + 3]
                back = CHECK_LIGHT if ((x // CHECK_SIZE + y // CHECK_SIZE) % 2 == 0) else CHECK_DARK
                for c in range(3):
                    value = (cell[src + c] * a + back * (255 - a) + 127) // 255
                    canvas[dst + x * 3 + c] = value

    path = out_path or os.path.join(out_dir, "contact.png")
    stride = sheet_w * 3
    png.write_rgb(path, sheet_w, sheet_h,
                  [canvas[i:i + stride] for i in range(0, len(canvas), stride)])
    return path


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("out_dir", help="an interp output directory")
    parser.add_argument("--every", type=int, default=8, help="use every Nth frame")
    parser.add_argument("--columns", type=int, default=10)
    parser.add_argument("--scale", type=int, default=2, help="shrink each cell by this factor")
    parser.add_argument("--out", help="path of the sheet, default <out_dir>/contact.png")
    args = parser.parse_args(argv)
    print(sheet(args.out_dir, args.every, args.columns, args.scale, args.out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

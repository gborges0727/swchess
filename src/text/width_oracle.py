"""Print the pixel width of a line drawn with LEGFONT or GUITEXT.

text_test runs this to check its own layout against the Python that first read
the two fonts. Both sides answer the same question: how wide does this run of
stored bytes draw, counting the one blank column after every glyph. This side
reads the widths and the cells through tools/extract, so nothing but the CD
files is shared with the C++ code.

    python3 src/text/width_oracle.py --cd original/win3x/cd legfont:496c20 guitext:4b5c4e

Each argument names a font and the stored bytes as hex. One width per line comes
back, in the order the arguments arrive.
"""

import argparse
import os
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, REPO_ROOT)

from tools.fonts import legfont  # noqa: E402

MISSING_ADVANCE = 14
GUITEXT_SPACE_ADVANCE = 5


def legfont_advances(cd_dir):
    """Return the advance of each LEGFONT cell, or None where it draws nothing."""
    widths = legfont.read_legfont_widths(cd_dir)
    return [w if w else None for w in widths]


def guitext_advances(cd_dir):
    """Return the advance of each GUITEXT cell, measured off the strip."""
    strip = legfont.load_guitext(cd_dir)
    out = []
    for index in range(legfont.GLYPH_COUNT):
        cell = legfont._pixels(strip, legfont.guitext_cell(index))
        ink = legfont.measure_ink(cell, legfont.GUITEXT_BACKGROUND)
        out.append(ink + 1 if ink else None)
    out[0] = GUITEXT_SPACE_ADVANCE
    return out


def line_width(advances, raw):
    """Return how wide these stored bytes draw with one font."""
    total = 0
    for byte in raw:
        index = byte - legfont.FIRST_CODE
        advance = None
        if 0 <= index < legfont.GLYPH_COUNT:
            advance = advances[index]
        total += (advance if advance else MISSING_ADVANCE) + 1
    return total


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cd", default=os.path.join(REPO_ROOT, "original/win3x/cd"))
    parser.add_argument("lines", nargs="+", help="font:hex, with font legfont or guitext")
    args = parser.parse_args()

    tables = {"legfont": legfont_advances(args.cd), "guitext": guitext_advances(args.cd)}
    for line in args.lines:
        name, _, payload = line.partition(":")
        if name not in tables:
            raise SystemExit(f"unknown font {name!r}")
        print(line_width(tables[name], bytes.fromhex(payload)))


if __name__ == "__main__":
    main()

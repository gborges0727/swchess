"""Slice the two Star Wars Chess bitmap fonts and draw sample lines with them.

The game keeps its text in four resource DLLs as plain ASCII bytes, but several
byte values stand for glyphs that are not the ASCII character. Two different
bitmap fonts read those bytes, and each font gives some of them a different
glyph, so the mapping depends on which screen draws the string.

LEGFONT lives in TITLERES.DLL at file offset 0xe2c00. It draws the opening crawl
(string ids 14000 to 14031) and the credit roll (ids 14992 to 15199). It has no
digits: the cells at '0' to '9' hold accented lowercase letters, and the cells at
'[' and '\\' hold the shapes of the digits 1 and 0.

GUITEXT lives in CC16.DLL and CC256.DLL as a bitmap resource of that name. It
draws every other string, which means all the menus and all the move messages.
Its digit cells really do hold digits, and its '[', '\\' and ']' cells hold the
German capitals U-umlaut, O-umlaut and A-umlaut.

Run it like this:

    python3 -m tools.fonts.legfont --cd original/win3x/cd --out tools/fonts/preview
"""

import argparse
import os
import struct

from ..extract import dib, ne, png

LEGFONT_OFFSET = 0xE2C00
LEGFONT_COLUMNS = 16
LEGFONT_CELL_W = 40
LEGFONT_CELL_H = 42
LEGFONT_WIDTHS_OFFSET = 0x36EC4  # 96 little-endian words inside XCHESS.EXE
LEGFONT_BACKGROUND = 159

GUITEXT_CELL_W = 16
GUITEXT_CELL_H = 17
GUITEXT_BACKGROUND = 159
GUITEXT_GRID = 25  # the color of the separator lines drawn through the strip

FIRST_CODE = 0x20
GLYPH_COUNT = 96

# Byte value to the character a reader sees, for each font. Every byte not
# listed keeps its ASCII meaning.
LEGFONT_MAP = {
    "0": "ä",  # a umlaut
    "1": "à",  # a grave
    "2": "è",  # e grave
    "3": "é",  # e acute
    "4": "ê",  # e circumflex
    "5": "ö",  # o umlaut
    "6": "ü",  # u umlaut
    "7": "á",  # a acute
    "8": "í",  # i acute
    "9": "ß",  # sharp s
    "[": "1",
    "\\": "0",
    "]": "©",  # copyright sign
    "^": "®",  # registered sign
}

GUITEXT_MAP = {
    "[": "Ü",  # U umlaut
    "\\": "Ö",  # O umlaut
    "]": "Ä",  # A umlaut
}


def _reverse(mapping):
    out = {}
    for code, shown in mapping.items():
        out[shown] = code
    return out


def load_legfont(cd_dir):
    """Return the LEGFONT bitmap out of TITLERES.DLL."""
    with open(os.path.join(cd_dir, "TITLERES.DLL"), "rb") as fh:
        blob = fh.read()
    return dib.parse_dib(blob, LEGFONT_OFFSET)


def load_guitext(cd_dir, name="CC256.DLL"):
    """Return the GUITEXT bitmap out of CC256.DLL or CC16.DLL."""
    blob, resources = ne.read_file(os.path.join(cd_dir, name))
    for res in resources:
        if res.name_id == "GUITEXT":
            return dib.parse_dib(blob, res.offset)
    raise ValueError(f"{name} holds no GUITEXT resource")


def read_legfont_widths(cd_dir):
    """Return the 96 advance widths XCHESS.EXE keeps for LEGFONT."""
    with open(os.path.join(cd_dir, "XCHESS.EXE"), "rb") as fh:
        blob = fh.read()
    cell_w, cell_h = struct.unpack_from("<HH", blob, LEGFONT_WIDTHS_OFFSET - 6)
    if (cell_w, cell_h) != (LEGFONT_CELL_W, LEGFONT_CELL_H):
        raise ValueError(f"expected a 40 by 42 cell ahead of the widths, got {cell_w} by {cell_h}")
    return list(struct.unpack_from(f"<{GLYPH_COUNT}H", blob, LEGFONT_WIDTHS_OFFSET))


def legfont_cell(index):
    """Return the (x, y, w, h) of one LEGFONT glyph cell, separators excluded."""
    row, col = divmod(index, LEGFONT_COLUMNS)
    return (col * LEGFONT_CELL_W + 1, row * LEGFONT_CELL_H + 1, LEGFONT_CELL_W - 1, LEGFONT_CELL_H - 1)


def guitext_cell(index):
    """Return the (x, y, w, h) of one GUITEXT glyph cell, separators excluded."""
    return (index * GUITEXT_CELL_W, 0, GUITEXT_CELL_W - 1, GUITEXT_CELL_H - 1)


def _pixels(bitmap, box):
    x0, y0, w, h = box
    return [[dib.index_at(bitmap, x0 + x, y0 + y) for x in range(w)] for y in range(h)]


def _rgb(bitmap, value):
    pal = bitmap["palette"]
    return (pal[value * 4 + 2], pal[value * 4 + 1], pal[value * 4])


def measure_ink(cell, background):
    """Return how many columns of a cell carry ink."""
    last = -1
    for row in cell:
        for x, value in enumerate(row):
            if value != background and x > last:
                last = x
    return last + 1


def _write_cell(path, cell, bitmap, background):
    h = len(cell)
    w = len(cell[0]) if h else 0
    rows = []
    for row in cell:
        out = bytearray()
        for value in row:
            r, g, b = _rgb(bitmap, value)
            out += bytes((r, g, b, 0 if value == background else 255))
        rows.append(bytes(out))
    png.write_rgba(path, w, h, rows)


def _draw_line(text, cells, advances, background, ink, height, missing):
    """Compose one line of pixels and return (width, rows) as RGBA."""
    pieces = []
    for ch in text:
        code = missing.get(ch)
        if code is None:
            pieces.append((None, 14))
            continue
        index = ord(code) - FIRST_CODE
        pieces.append((index, advances[index]))
    width = sum(adv + 1 for _, adv in pieces) or 1
    canvas = [[None] * width for _ in range(height)]
    pen = 0
    for index, adv in pieces:
        if index is None:
            for y in range(height):
                canvas[y][pen] = "box"
                canvas[y][pen + adv - 1] = "box"
            for x in range(pen, pen + adv):
                canvas[0][x] = "box"
                canvas[height - 1][x] = "box"
        else:
            cell = cells[index]
            for y, row in enumerate(cell):
                if y >= height:
                    break
                for x, value in enumerate(row):
                    if x >= adv + 4:
                        break
                    if value != background and pen + x < width:
                        canvas[y][pen + x] = value
        pen += adv + 1
    rows = []
    for y in range(height):
        out = bytearray()
        for value in canvas[y]:
            if value is None:
                out += bytes((0, 0, 0, 255))
            elif value == "box":
                out += bytes((255, 0, 0, 255))
            else:
                out += bytes(ink(value)) + b"\xff"
        rows.append(bytes(out))
    return width, rows


def _stack(path, lines, pad=6):
    """Write several (width, rows) lines under each other into one PNG."""
    width = max(w for w, _ in lines)
    rows = []
    for w, line in lines:
        for row in line:
            rows.append(row + b"\x00\x00\x00\xff" * (width - w))
        for _ in range(pad):
            rows.append(b"\x00\x00\x00\xff" * width)
    png.write_rgba(path, width, len(rows), rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cd", default="original/win3x/cd")
    parser.add_argument("--out", default="tools/fonts/preview")
    args = parser.parse_args()

    leg = load_legfont(args.cd)
    gui = load_guitext(args.cd)
    widths = read_legfont_widths(args.cd)

    leg_cells = [_pixels(leg, legfont_cell(i)) for i in range(GLYPH_COUNT)]
    gui_cells = [_pixels(gui, guitext_cell(i)) for i in range(GLYPH_COUNT)]
    gui_widths = [measure_ink(c, GUITEXT_BACKGROUND) + 1 for c in gui_cells]

    leg_dir = os.path.join(args.out, "legfont")
    gui_dir = os.path.join(args.out, "guitext")
    os.makedirs(leg_dir, exist_ok=True)
    os.makedirs(gui_dir, exist_ok=True)
    for i in range(GLYPH_COUNT):
        code = FIRST_CODE + i
        _write_cell(os.path.join(leg_dir, f"{code:02x}.png"), leg_cells[i], leg, LEGFONT_BACKGROUND)
        _write_cell(os.path.join(gui_dir, f"{code:02x}.png"), gui_cells[i], gui, GUITEXT_BACKGROUND)

    leg_rev = _reverse(LEGFONT_MAP)
    for i, w in enumerate(widths):
        ch = chr(FIRST_CODE + i)
        if w and ch not in LEGFONT_MAP:
            leg_rev.setdefault(ch, ch)
    gui_rev = _reverse(GUITEXT_MAP)
    for i, cell in enumerate(gui_cells):
        ch = chr(FIRST_CODE + i)
        if measure_ink(cell, GUITEXT_BACKGROUND) and ch not in GUITEXT_MAP:
            gui_rev.setdefault(ch, ch)
    gui_rev.setdefault(" ", " ")
    gui_widths[0] = 5

    sample = "KÖNIG Läufer très Peón 0123"
    leg_line = _draw_line(
        sample, leg_cells, widths, LEGFONT_BACKGROUND, lambda v: _rgb(leg, v), LEGFONT_CELL_H - 1, leg_rev
    )
    gui_line = _draw_line(
        sample, gui_cells, gui_widths, GUITEXT_BACKGROUND, lambda v: _rgb(gui, v), GUITEXT_CELL_H - 1, gui_rev
    )
    _stack(os.path.join(args.out, "sample.png"), [leg_line])
    _stack(os.path.join(args.out, "sample_guitext.png"), [gui_line])

    crawl = "Il y a très longtemps dans une"
    menu = "KÖNIG BEWEGT - KEINE ROCHADE"
    _stack(
        os.path.join(args.out, "sample_real_strings.png"),
        [
            _draw_line(crawl, leg_cells, widths, LEGFONT_BACKGROUND, lambda v: _rgb(leg, v), LEGFONT_CELL_H - 1, leg_rev),
            _draw_line(menu, gui_cells, gui_widths, GUITEXT_BACKGROUND, lambda v: _rgb(gui, v), GUITEXT_CELL_H - 1, gui_rev),
        ],
    )
    print(f"wrote {GLYPH_COUNT} LEGFONT cells and {GLYPH_COUNT} GUITEXT cells under {args.out}")


if __name__ == "__main__":
    main()

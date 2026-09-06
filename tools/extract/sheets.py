"""Convert the eight loose BMP files.

Four of them are piece sheets that CM.INI names in its [chesssets] section, and
four are full screen backgrounds. A sheet holds twelve characters in two rows of
six. The cell sizes come from [chesssets]. A one pixel separator sits in front of
each column and each row, so cell k starts at 1 + k * (cell + 1). The extractor
checks that every separator line really is one flat color and writes the result
into the manifest instead of assuming it.

Which column holds which piece is not established yet, so the manifest records
the grid position and marks the piece order as unverified.
"""

import hashlib
import json
import os

from . import dib, png
from .ini import IniFile

BACKGROUNDS = ["2DBDBTOP.BMP", "2DBDWTOP.BMP", "SPACE256.BMP", "THRON256.BMP"]

SHEET_FILES = {
    "2DSET_": "2DSET_P.BMP",
    "WHTBTM_": "WHTBTM_P.BMP",
    "WHTTOP_": "WHTTOP_P.BMP",
    "FACING_": "FACING_P.BMP",
}

COLUMNS = 6
ROWS = 2

# CM.INI [demo] numbers the attackers K=0 Q=1 R=2 B=3 N=4 P=5. The sheets have
# not been matched to that order yet, so the manifest marks this unverified.
ASSUMED_COLUMN_ORDER = ["K", "Q", "R", "B", "N", "P"]
ASSUMED_ROW_ORDER = ["white", "black"]


def _sha256(path):
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


def _column_is_flat(bitmap, x, limit):
    """Check one separator column over the grid only, not the spare canvas."""
    height = min(limit, bitmap["height"])
    first = dib.index_at(bitmap, x, 0)
    return all(dib.index_at(bitmap, x, y) == first for y in range(height)), first


def _row_is_flat(bitmap, y, limit):
    """Check one separator row over the grid only, not the spare canvas."""
    width = min(limit, bitmap["width"])
    first = dib.index_at(bitmap, 0, y)
    return all(dib.index_at(bitmap, x, y) == first for x in range(width)), first


def extract_sets(cd_dir, out_dir):
    """Write the four piece sheets plus their cells and return catalog data."""
    os.makedirs(out_dir, exist_ok=True)
    cm = IniFile(os.path.join(cd_dir, "CM.INI"))
    chesssets = cm.section("chesssets")
    entries = []

    for key, filename in SHEET_FILES.items():
        raw_value = chesssets.get(key) if chesssets else None
        tokens = raw_value.split() if raw_value else []
        cell_w = int(tokens[4]) if len(tokens) > 5 else None
        cell_h = int(tokens[5]) if len(tokens) > 5 else None

        src = os.path.join(cd_dir, filename)
        bitmap = dib.read_bmp_file(src)
        folder = os.path.join(out_dir, key.rstrip("_"))
        os.makedirs(folder, exist_ok=True)

        sheet_png = os.path.join(folder, "sheet.png")
        png.write_rgb(
            sheet_png,
            bitmap["width"],
            bitmap["height"],
            dib.to_rgb_rows(bitmap),
        )

        cells = []
        separators = {"columns": [], "rows": [], "all_flat": None}
        grid_w = COLUMNS * (cell_w + 1) if cell_w else 0
        grid_h = ROWS * (cell_h + 1) if cell_h else 0
        if cell_w and cell_h:
            flat = True
            for c in range(COLUMNS):
                x = c * (cell_w + 1)
                if x >= bitmap["width"]:
                    continue
                ok, index = _column_is_flat(bitmap, x, grid_h)
                separators["columns"].append({"x": x, "flat": ok, "palette_index": index})
                flat = flat and ok
            for r in range(ROWS):
                y = r * (cell_h + 1)
                if y >= bitmap["height"]:
                    continue
                ok, index = _row_is_flat(bitmap, y, grid_w)
                separators["rows"].append({"y": y, "flat": ok, "palette_index": index})
                flat = flat and ok
            separators["all_flat"] = flat

            for r in range(ROWS):
                for c in range(COLUMNS):
                    x = 1 + c * (cell_w + 1)
                    y = 1 + r * (cell_h + 1)
                    if x + cell_w > bitmap["width"] or y + cell_h > bitmap["height"]:
                        continue
                    name = f"r{r}c{c}.png"
                    rows = dib.to_rgba_rows(bitmap, (x, y, cell_w, cell_h))
                    png.write_rgba(os.path.join(folder, name), cell_w, cell_h, rows)
                    cells.append(
                        {
                            "row": r,
                            "column": c,
                            "x": x,
                            "y": y,
                            "width": cell_w,
                            "height": cell_h,
                            "image": name,
                            "assumed_color": ASSUMED_ROW_ORDER[r] if r < len(ASSUMED_ROW_ORDER) else None,
                            "assumed_piece": ASSUMED_COLUMN_ORDER[c] if c < len(ASSUMED_COLUMN_ORDER) else None,
                        }
                    )

        used_w = grid_w
        used_h = grid_h
        manifest = {
            "set": key,
            "source": filename,
            "source_sha256": _sha256(src),
            "cm_ini_value": raw_value,
            "cm_ini_tokens": tokens,
            "label": tokens[0] if tokens else None,
            "dimension": tokens[1] if len(tokens) > 1 else None,
            "sheet_width": bitmap["width"],
            "sheet_height": bitmap["height"],
            "cell_width": cell_w,
            "cell_height": cell_h,
            "columns": COLUMNS,
            "rows": ROWS,
            "slicing_rule": "cell k starts at 1 + k * (cell size + 1) on both axes",
            "separators": separators,
            "unused_canvas": {
                "right": bitmap["width"] - used_w,
                "bottom": bitmap["height"] - used_h,
            },
            "piece_order_verified": False,
            "piece_order_note": "column and color assignments come from the CM.INI [demo] numbering and still need code evidence",
            "transparent_index": 0,
            "sheet_image": f"sets/{key.rstrip('_')}/sheet.png",
            "cells": cells,
        }
        with open(os.path.join(folder, "manifest.json"), "w") as fh:
            json.dump(manifest, fh, indent=1)
        entries.append(manifest)

    return {"sets": entries, "set_count": len(entries)}


def extract_backgrounds(cd_dir, out_dir):
    """Write the four full screen backgrounds as opaque PNGs."""
    os.makedirs(out_dir, exist_ok=True)
    entries = []
    for filename in BACKGROUNDS:
        src = os.path.join(cd_dir, filename)
        bitmap = dib.read_bmp_file(src)
        name = os.path.splitext(filename)[0] + ".png"
        out = os.path.join(out_dir, name)
        png.write_rgb(out, bitmap["width"], bitmap["height"], dib.to_rgb_rows(bitmap))
        entries.append(
            {
                "source": filename,
                "source_sha256": _sha256(src),
                "width": bitmap["width"],
                "height": bitmap["height"],
                "bit_count": bitmap["bit_count"],
                "opaque": True,
                "output": f"backgrounds/{name}",
                "output_sha256": _sha256(out),
            }
        )
    return {"backgrounds": entries, "background_count": len(entries)}

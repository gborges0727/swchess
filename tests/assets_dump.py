"""Print a checksum line for every piece bitmap, sheet cell and WAVE record.

The C++ test in tests/assets_oracle_test.cpp reads the same original files with
src/assets and rebuilds these lines, so the two implementations never share
code. Comparing checksums of decoded pixels and samples keeps PNG and RIFF
container bytes out of the comparison.

Each line reads one of:

    PIECE <PIECE> <resource name> <offset hex> <length> <w> <h> <sha256 of RGBA>
    CELL <SET> r<row>c<column> <x> <y> <w> <h> <sha256 of RGBA>
    WAVE <index> <name> <offset hex> <riff length> <channels> <rate> <bits> <sha256 of samples>
    LOOSE <name> <channels> <rate> <bits> <sha256 of samples>
    TOTAL pieces <n> cells <n> waves <n> distinct <n>

Run it like this:

    python3 tests/assets_dump.py --cd original/win3x/cd --out build/assets_oracle.txt
"""

import argparse
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))

from tools.extract import audio, dib, ne  # noqa: E402
from tools.extract.ini import IniFile  # noqa: E402
from tools.extract.pieces import PIECES  # noqa: E402
from tools.extract.sheets import COLUMNS, ROWS, SHEET_FILES  # noqa: E402
from tools.reference import anx  # noqa: E402


def piece_lines(cd_dir):
    """One line per bitmap resource in each of the twelve piece DLLs."""
    lines = []
    for piece in PIECES:
        blob, resources = ne.read_file(os.path.join(cd_dir, piece + ".DLL"))
        for res in resources:
            if res.type_id != ne.RT_BITMAP:
                continue
            rec = anx.decode_record(blob, res.offset, res.offset + res.length)
            if not rec["complete"]:
                raise ValueError(f"{piece}.DLL resource {res.name_id} decoded short")
            rgba = b"".join(anx.to_rgba(rec))
            digest = hashlib.sha256(rgba).hexdigest()
            lines.append(
                f"PIECE {piece} {res.name_id} {res.offset:#x} {res.length} "
                f"{rec['width']} {rec['height']} {digest}"
            )
    return lines


def cell_lines(cd_dir):
    """One line per sliced cell in each of the four piece sheets."""
    cm = IniFile(os.path.join(cd_dir, "CM.INI"))
    chesssets = cm.section("chesssets")
    lines = []
    for key, filename in SHEET_FILES.items():
        tokens = chesssets.get(key).split()
        cell_w, cell_h = int(tokens[4]), int(tokens[5])
        bitmap = dib.read_bmp_file(os.path.join(cd_dir, filename))
        for r in range(ROWS):
            for c in range(COLUMNS):
                x = 1 + c * (cell_w + 1)
                y = 1 + r * (cell_h + 1)
                if x + cell_w > bitmap["width"] or y + cell_h > bitmap["height"]:
                    continue
                rgba = b"".join(dib.to_rgba_rows(bitmap, (x, y, cell_w, cell_h)))
                digest = hashlib.sha256(rgba).hexdigest()
                name = key.rstrip("_")
                lines.append(
                    f"CELL {name} r{r}c{c} {x} {y} {cell_w} {cell_h} {digest}"
                )
    return lines


def data_chunk(blob):
    """Return the bytes of the data chunk in one RIFF WAVE held in memory."""
    pos = 12
    while pos + 8 <= len(blob):
        tag = blob[pos : pos + 4]
        size = int.from_bytes(blob[pos + 4 : pos + 8], "little")
        body = pos + 8
        if tag == b"data":
            return blob[body : body + size]
        pos = body + size + (size & 1)
    return b""


def wave_lines(cd_dir):
    """One line per WAVE resource in SWCAUDIO.DLL, then the four loose files."""
    blob, resources = ne.read_file(os.path.join(cd_dir, "SWCAUDIO.DLL"))
    lines = []
    index = 0
    names = set()
    for res in resources:
        if res.type_id != "WAVE":
            continue
        length = audio.riff_length(blob, res.offset)
        data = blob[res.offset : res.offset + length]
        info = audio.describe_wave(data)
        digest = hashlib.sha256(data_chunk(data)).hexdigest()
        names.add(res.name_id)
        lines.append(
            f"WAVE {index} {res.name_id} {res.offset:#x} {length} "
            f"{info['channels']} {info['sample_rate']} {info['bits_per_sample']} {digest}"
        )
        index += 1

    for name in audio.STANDALONE:
        with open(os.path.join(cd_dir, name), "rb") as fh:
            data = fh.read()
        info = audio.describe_wave(data)
        digest = hashlib.sha256(data_chunk(data)).hexdigest()
        lines.append(
            f"LOOSE {name} {info['channels']} {info['sample_rate']} "
            f"{info['bits_per_sample']} {digest}"
        )
    return lines, index, len(names)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cd", required=True, help="directory holding the original CD files")
    ap.add_argument("--out", required=True, help="file to write the checksum lines into")
    args = ap.parse_args(argv)

    pieces = piece_lines(args.cd)
    cells = cell_lines(args.cd)
    waves, wave_count, distinct = wave_lines(args.cd)
    lines = pieces + cells + waves
    lines.append(
        f"TOTAL pieces {len(pieces)} cells {len(cells)} waves {wave_count} distinct {distinct}"
    )

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    print(
        f"wrote {args.out} with {len(pieces)} piece bitmaps, {len(cells)} sheet cells "
        f"and {wave_count} WAVE records under {distinct} names"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

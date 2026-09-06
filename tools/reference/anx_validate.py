"""Check every ANX file in a directory against the decoder in anx.py.

Run it like this:

    python3 tools/reference/anx_validate.py --cd original/win3x/cd

Add --sheets DIR to write contact sheets for a few captures. Without that flag
the script writes nothing.
"""

import argparse
import collections
import glob
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from anx import decode_record, load, to_rgb  # noqa: E402


def write_png_rgb(path, width, height, rows):
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(tag, body):
        c = tag + body
        return struct.pack(">I", len(body)) + c + struct.pack(">I", zlib.crc32(c))

    with open(path, "wb") as fh:
        fh.write(b"\x89PNG\r\n\x1a\n")
        fh.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        fh.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        fh.write(chunk(b"IEND", b""))


def contact_sheet(anx_path, out_path, max_frames=None, columns=10):
    count, offsets, records = load(anx_path)
    picks = list(range(count))
    if max_frames is not None and count > max_frames:
        step = max(1, count // max_frames)
        picks = list(range(0, count, step))[:max_frames]
    cell_w = max(r["width"] for r in records.values())
    cell_h = max(r["height"] for r in records.values())
    rows_n = (len(picks) + columns - 1) // columns
    width = columns * cell_w
    height = rows_n * cell_h
    canvas = [bytearray(b"\x30\x30\x30" * width) for _ in range(height)]
    for j, frame in enumerate(picks):
        rec = records[offsets[frame]]
        pixels = to_rgb(rec)
        cx = (j % columns) * cell_w
        cy = (j // columns) * cell_h
        for y, row in enumerate(pixels):
            target = canvas[cy + cell_h - rec["height"] + y]
            target[cx * 3 : (cx + rec["width"]) * 3] = row
    write_png_rgb(out_path, width, height, [bytes(c) for c in canvas])
    print("sheet", out_path, width, "x", height, "frames", len(picks))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cd", required=True, help="directory holding the ANX files")
    ap.add_argument("--sheets", help="directory to write contact sheets into")
    args = ap.parse_args(argv)

    total = 0
    bad_length = 0
    bad_tail = 0
    long_tail = 0
    escapes = collections.Counter()
    escape_in_pixels = 0
    reused = 0
    timeline = 0

    for path in sorted(glob.glob(os.path.join(args.cd, "*.ANX"))):
        count, offsets, records = load(path)
        timeline += count
        reused += count - len(records)
        for off, rec in records.items():
            total += 1
            escapes[rec["escape"]] += 1
            if not rec["complete"]:
                bad_length += 1
                print("LEN", os.path.basename(path), hex(off), len(rec["pixels"]))
            if not rec["tail_zero"]:
                bad_tail += 1
                print("TAIL", os.path.basename(path), hex(off), rec["tail"][:16].hex())
            if len(rec["tail"]) > 31:
                long_tail += 1
                print("LONGTAIL", os.path.basename(path), hex(off), len(rec["tail"]))
            if rec["escape"] in rec["pixels"]:
                escape_in_pixels += 1
            if rec["size_image"] != rec["stride"] * rec["height"]:
                print("SIZEIMAGE", os.path.basename(path), hex(off), rec["size_image"])

    print("records", total, "timeline", timeline, "reused references", reused)
    print("escape values", dict(escapes))
    print(
        "wrong length", bad_length,
        "nonzero tail", bad_tail,
        "tail over 31 bytes", long_tail,
        "escape index inside pixels", escape_in_pixels,
    )

    if args.sheets:
        os.makedirs(args.sheets, exist_ok=True)
        contact_sheet(os.path.join(args.cd, "BBWB.ANX"), os.path.join(args.sheets, "bbwb_all.png"))
        contact_sheet(os.path.join(args.cd, "BKWB.ANX"), os.path.join(args.sheets, "bkwb_all.png"))
        contact_sheet(os.path.join(args.cd, "WBBR.ANX"), os.path.join(args.sheets, "wbbr_sample.png"), 30)
    return 0 if bad_length == 0 and bad_tail == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())

"""Print a checksum line for every distinct ANX record on the CD.

The C++ test in tests/anx_oracle_test.cpp decodes the same files and compares
its own lines against these, so the two decoders never share code.

Each ANX line reads:

    ANX <file> <offset hex> <width> <height> <sha256 of the raw index bytes>

Each BMP line reads:

    BMP <file> <width> <height> <sha256 of the top-down RGBA bytes>

Run it like this:

    python3 tests/anx_dump.py --cd original/win3x/cd --out build/anx_oracle.txt
"""

import argparse
import glob
import hashlib
import os
import struct
import sys

sys.path.insert(
    0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "reference")
)

from anx import load  # noqa: E402


def bmp_rgba(path):
    """Decode an 8-bit Windows BMP to top-down RGBA with alpha 255."""
    with open(path, "rb") as fh:
        data = fh.read()
    pixels_at = struct.unpack_from("<I", data, 10)[0]
    header_size = struct.unpack_from("<I", data, 14)[0]
    width, raw_height = struct.unpack_from("<ii", data, 18)
    bit_count = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    if bit_count != 8 or compression != 0:
        raise ValueError("expected an uncompressed 8-bit BMP: " + path)
    top_down = raw_height < 0
    height = -raw_height if top_down else raw_height
    palette_at = 14 + header_size
    clr_used = struct.unpack_from("<I", data, 46)[0] or 256
    table = [
        bytes((data[palette_at + i * 4 + 2], data[palette_at + i * 4 + 1], data[palette_at + i * 4], 255))
        for i in range(clr_used)
    ]
    stride = (width + 3) & ~3
    out = bytearray()
    for y in range(height):
        source = y if top_down else height - 1 - y
        row = data[pixels_at + source * stride : pixels_at + source * stride + width]
        out += b"".join(table[p] for p in row)
    return width, height, bytes(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cd", required=True, help="directory holding the ANX and BMP files")
    ap.add_argument("--out", required=True, help="file to write the checksum lines into")
    args = ap.parse_args(argv)

    lines = []
    records = 0
    timeline = 0
    for path in sorted(glob.glob(os.path.join(args.cd, "*.ANX"))):
        name = os.path.basename(path)
        count, _offsets, recs = load(path)
        timeline += count
        for off in sorted(recs):
            rec = recs[off]
            records += 1
            digest = hashlib.sha256(rec["pixels"]).hexdigest()
            lines.append(f"ANX {name} {off:#x} {rec['width']} {rec['height']} {digest}")

    for path in sorted(glob.glob(os.path.join(args.cd, "*.BMP"))):
        name = os.path.basename(path)
        width, height, rgba = bmp_rgba(path)
        lines.append(f"BMP {name} {width} {height} {hashlib.sha256(rgba).hexdigest()}")

    lines.append(f"TOTAL records {records} timeline {timeline}")
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    print(f"wrote {args.out} with {records} ANX records and {timeline} timeline entries")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

"""Read back a PNG this extractor wrote, so the self test can check its pixels.

This reader only handles what png.py produces: 8 bits per channel, no interlace,
a single IDAT chunk and filter type 0 on every row. It is a checking tool, not a
general PNG decoder.
"""

import struct
import zlib


def read_png(path):
    """Return width, height, channel count and the raw top-down pixel rows."""
    with open(path, "rb") as fh:
        blob = fh.read()
    if blob[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path} is not a PNG")
    pos = 8
    width = height = None
    channels = None
    idat = b""
    while pos < len(blob):
        length = struct.unpack_from(">I", blob, pos)[0]
        tag = blob[pos + 4 : pos + 8]
        body = blob[pos + 8 : pos + 8 + length]
        pos += 12 + length
        if tag == b"IHDR":
            width, height, depth, color_type = struct.unpack_from(">IIBB", body, 0)
            if depth != 8:
                raise ValueError("expected 8 bits per channel")
            channels = {2: 3, 6: 4}.get(color_type)
            if channels is None:
                raise ValueError(f"unexpected color type {color_type}")
        elif tag == b"IDAT":
            idat += body
        elif tag == b"IEND":
            break
    raw = zlib.decompress(idat)
    row_bytes = width * channels
    rows = []
    for y in range(height):
        start = y * (row_bytes + 1)
        if raw[start] != 0:
            raise ValueError("expected filter type 0")
        rows.append(raw[start + 1 : start + 1 + row_bytes])
    return width, height, channels, rows


def alpha_matches_index(path, record, transparent_index=0):
    """Check that a sprite PNG is clear exactly where the source index is 0.

    `record` is a decoded ANX record. Its pixels run bottom-up, so the check
    walks the PNG rows in reverse.
    """
    width, height, channels, rows = read_png(path)
    if channels != 4:
        return False, "the PNG is not RGBA"
    if width != record["width"] or height != record["height"]:
        return False, "the PNG size does not match the record"
    pixels = record["pixels"]
    for y in range(height):
        row = rows[height - 1 - y]
        line = pixels[y * width : (y + 1) * width]
        for x in range(width):
            alpha = row[x * 4 + 3]
            clear = line[x] == transparent_index
            if clear and alpha != 0:
                return False, f"index 0 at {x},{y} kept alpha {alpha}"
            if not clear and alpha != 255:
                return False, f"index {line[x]} at {x},{y} got alpha {alpha}"
    return True, "alpha is 0 exactly where the palette index is 0"

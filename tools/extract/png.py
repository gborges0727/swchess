"""Write PNG files with the standard library only.

Every function takes top-down rows of packed bytes and writes one 8-bit PNG.
The extractor uses these instead of Pillow so a fresh checkout needs no install.
"""

import struct
import zlib


def _chunk(tag, body):
    payload = tag + body
    return struct.pack(">I", len(body)) + payload + struct.pack(">I", zlib.crc32(payload))


def _write(path, width, height, color_type, rows, level=6):
    raw = b"".join(b"\x00" + row for row in rows)
    header = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)
    with open(path, "wb") as fh:
        fh.write(b"\x89PNG\r\n\x1a\n")
        fh.write(_chunk(b"IHDR", header))
        fh.write(_chunk(b"IDAT", zlib.compress(raw, level)))
        fh.write(_chunk(b"IEND", b""))


def write_rgba(path, width, height, rows, level=6):
    """Write an RGBA PNG. Each row holds width * 4 bytes."""
    _write(path, width, height, 6, rows, level)


def write_rgb(path, width, height, rows, level=6):
    """Write an opaque RGB PNG. Each row holds width * 3 bytes."""
    _write(path, width, height, 2, rows, level)

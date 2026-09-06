"""Read and write 8-bit PNG files with the standard library only.

The writer follows the same zlib recipe as tools/extract/png.py and
tools/rife/png_util.py. The reader is new here because nothing else in the repo
reads a full pixel buffer back, and the interpolation pipeline has to read what
rife-ncnn-vulkan wrote. tools/interp does not import tools/extract, so the two
lanes can change independently.
"""

import struct
import zlib

_MAGIC = b"\x89PNG\r\n\x1a\n"
_CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}


def _chunk(tag, body):
    payload = tag + body
    return struct.pack(">I", len(body)) + payload + struct.pack(">I", zlib.crc32(payload))


def _write(path, width, height, color_type, rows, level):
    raw = b"".join(b"\x00" + bytes(row) for row in rows)
    header = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)
    with open(path, "wb") as fh:
        fh.write(_MAGIC)
        fh.write(_chunk(b"IHDR", header))
        fh.write(_chunk(b"IDAT", zlib.compress(raw, level)))
        fh.write(_chunk(b"IEND", b""))


def write_rgba(path, width, height, rows, level=6):
    """Write an RGBA PNG from top-down rows of width * 4 bytes."""
    _write(path, width, height, 6, rows, level)


def write_rgb(path, width, height, rows, level=6):
    """Write an opaque RGB PNG from top-down rows of width * 3 bytes."""
    _write(path, width, height, 2, rows, level)


def write_gray(path, width, height, rows, level=6):
    """Write an 8-bit grayscale PNG from top-down rows of width bytes."""
    _write(path, width, height, 0, rows, level)


def read(path):
    """Return (width, height, channels, pixels) with pixels as one bytearray.

    The pixels run top-down, row by row, with no padding between rows. Only
    8-bit non-interlaced PNGs are supported, which covers everything this
    pipeline writes and everything rife-ncnn-vulkan writes.
    """
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:8] != _MAGIC:
        raise ValueError("not a PNG: %s" % path)
    pos = 8
    header = None
    idat = []
    while pos + 8 <= len(data):
        length = struct.unpack_from(">I", data, pos)[0]
        tag = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if tag == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif tag == b"IDAT":
            idat.append(body)
        elif tag == b"IEND":
            break
        pos += 12 + length
    width, height, depth, color, _, _, interlace = header
    if depth != 8 or interlace != 0 or color == 3:
        raise ValueError("unsupported PNG form in %s" % path)
    channels = _CHANNELS[color]
    raw = zlib.decompress(b"".join(idat))
    return width, height, channels, _unfilter(raw, width, height, channels)


def _unfilter(raw, width, height, channels):
    stride = width * channels
    out = bytearray(height * stride)
    prev = bytes(stride)
    pos = 0
    for y in range(height):
        filt = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        if filt == 0:
            pass
        elif filt == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif filt == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif filt == 3:
            for i in range(channels):
                line[i] = (line[i] + (prev[i] >> 1)) & 0xFF
            for i in range(channels, stride):
                line[i] = (line[i] + ((line[i - channels] + prev[i]) >> 1)) & 0xFF
        elif filt == 4:
            for i in range(channels):
                line[i] = (line[i] + prev[i]) & 0xFF
            for i in range(channels, stride):
                a = line[i - channels]
                b = prev[i]
                c = prev[i - channels]
                p = a + b - c
                pa = abs(p - a)
                pb = abs(p - b)
                pc = abs(p - c)
                if pa <= pb and pa <= pc:
                    pr = a
                elif pb <= pc:
                    pr = b
                else:
                    pr = c
                line[i] = (line[i] + pr) & 0xFF
        else:
            raise ValueError("unknown PNG filter %d" % filt)
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return out

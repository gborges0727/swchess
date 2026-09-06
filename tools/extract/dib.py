"""Read plain Windows bitmaps.

Two shapes show up in the game files. A .BMP file starts with a BITMAPFILEHEADER
and then a BITMAPINFOHEADER. A bitmap resource inside TITLERES.DLL, CC16.DLL or
CC256.DLL starts at the BITMAPINFOHEADER with no file header. Both store 8-bit
indices bottom-up with rows padded to 4 bytes, and both use biCompression 0.
"""

import struct


def parse_dib(blob, start, pixel_offset=None):
    """Parse one uncompressed 8-bit DIB and return a dict describing it."""
    header_size, width, height, planes, bit_count = struct.unpack_from("<IiiHH", blob, start)
    comp, size_image, xppm, yppm, clr_used, clr_important = struct.unpack_from(
        "<IIiiII", blob, start + 16
    )
    if comp != 0:
        raise ValueError(f"expected an uncompressed DIB, got biCompression {comp:#x}")
    if bit_count != 8:
        raise ValueError(f"expected 8 bits per pixel, got {bit_count}")
    entries = clr_used if clr_used else 256
    pal_start = start + header_size
    palette = blob[pal_start : pal_start + entries * 4]
    if pixel_offset is None:
        pixel_offset = pal_start + entries * 4
    stride = (width + 3) & ~3
    pixels = blob[pixel_offset : pixel_offset + stride * abs(height)]
    return {
        "width": width,
        "height": abs(height),
        "top_down": height < 0,
        "bit_count": bit_count,
        "palette": palette,
        "clr_used": clr_used,
        "size_image": size_image,
        "stride": stride,
        "pixels": pixels,
        "pixel_offset": pixel_offset,
    }


def read_bmp_file(path):
    """Read a .BMP file from disk and return the same dict as parse_dib."""
    with open(path, "rb") as fh:
        blob = fh.read()
    if blob[:2] != b"BM":
        raise ValueError(f"{path} does not start with BM")
    pixel_offset = struct.unpack_from("<I", blob, 10)[0]
    dib = parse_dib(blob, 14, pixel_offset)
    dib["file_size"] = len(blob)
    return dib


def index_at(dib, x, y):
    """Return the palette index at a top-left origin coordinate."""
    row = y if dib["top_down"] else dib["height"] - 1 - y
    return dib["pixels"][row * dib["stride"] + x]


def to_rgb_rows(dib, region=None):
    """Return top-down RGB rows for the whole bitmap or one (x, y, w, h) region."""
    pal = dib["palette"]
    table = [bytes((pal[i * 4 + 2], pal[i * 4 + 1], pal[i * 4])) for i in range(len(pal) // 4)]
    if region is None:
        rx, ry, rw, rh = 0, 0, dib["width"], dib["height"]
    else:
        rx, ry, rw, rh = region
    stride = dib["stride"]
    rows = []
    for y in range(ry, ry + rh):
        src = y if dib["top_down"] else dib["height"] - 1 - y
        base = src * stride
        line = dib["pixels"][base + rx : base + rx + rw]
        rows.append(b"".join(table[p] for p in line))
    return rows


def to_rgba_rows(dib, region=None, transparent_index=0):
    """Return top-down RGBA rows, with one palette index turned fully clear."""
    pal = dib["palette"]
    table = []
    for i in range(len(pal) // 4):
        alpha = 0 if i == transparent_index else 255
        table.append(bytes((pal[i * 4 + 2], pal[i * 4 + 1], pal[i * 4], alpha)))
    if region is None:
        rx, ry, rw, rh = 0, 0, dib["width"], dib["height"]
    else:
        rx, ry, rw, rh = region
    stride = dib["stride"]
    rows = []
    for y in range(ry, ry + rh):
        src = y if dib["top_down"] else dib["height"] - 1 - y
        base = src * stride
        line = dib["pixels"][base + rx : base + rx + rw]
        rows.append(b"".join(table[p] for p in line))
    return rows

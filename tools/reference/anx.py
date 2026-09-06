"""Reference decoder for the Star Wars Chess ANX capture files.

An ANX file starts with a frame count at offset 0, then three tables of 150
little-endian entries each. Table 1 at offset 0x004 holds each record's byte
offset relative to 0x70c, where the first BITMAPINFOHEADER sits. Table 2 at
offset 0x25c holds each record's position as two signed 16-bit values, x then y.
Table 3 at offset 0x4b4 holds each record's compressed byte length, which only
the packer needed. Several timeline entries may point at the same table 1
offset, so the file stores each distinct bitmap once.

Each record is a BITMAPINFOHEADER followed by a BGRX palette and escape-byte RLE
pixel data. The high word of the biCompression dword picks the escape byte. A
run reads as escape, value, count. Any other byte is one literal pixel. Rows are
packed tight at width bytes per row, so there is no 4-byte DIB stride. Rows run
bottom-up like an ordinary DIB. Palette index 0 is the transparent color.

This module never writes files. Import it and call load() or decode_record().
"""

import struct

# Every ANX offset slot counts from this file offset.
BASE = 0x70C

# Each of the three tables holds 150 slots whether or not the capture uses them
# all, so the three tables together fill the 1800 bytes in front of BASE.
TABLE_SLOTS = 150
OFFSET_TABLE = 0x004
POSITION_TABLE = 0x25C
LENGTH_TABLE = 0x4B4


def positions(data, count=None):
    """Read table 2 as a list of (x, y) pairs of signed 16-bit values."""
    n = TABLE_SLOTS if count is None else count
    return [
        struct.unpack_from("<hh", data, POSITION_TABLE + 4 * i) for i in range(n)
    ]


def compressed_lengths(data, count=None):
    """Read table 3, the compressed byte length the packer wrote per record."""
    n = TABLE_SLOTS if count is None else count
    return [
        struct.unpack_from("<I", data, LENGTH_TABLE + 4 * i)[0] for i in range(n)
    ]


def decode_record(data, start, end):
    """Decode one bitmap record out of `data` between `start` and `end`.

    `start` points at the BITMAPINFOHEADER. `end` is where the next record
    starts, or the end of the buffer for the last record.
    """
    size, width, height, planes, bit_count = struct.unpack_from("<IiiHH", data, start)
    comp, size_image, xppm, yppm, clr_used, clr_important = struct.unpack_from(
        "<IIiiII", data, start + 16
    )
    escape = comp >> 16
    palette_entries = clr_used if clr_used else (1 << bit_count if bit_count <= 8 else 0)
    pal_start = start + size
    palette = data[pal_start : pal_start + palette_entries * 4]
    i = pal_start + palette_entries * 4

    need = width * height
    out = bytearray()
    while len(out) < need and i < end:
        b = data[i]
        i += 1
        if b == escape:
            value = data[i]
            count = data[i + 1]
            i += 2
            out += bytes([value]) * count
        else:
            out.append(b)

    tail = data[i:end]
    return {
        "width": width,
        "height": height,
        "planes": planes,
        "bit_count": bit_count,
        "compression": comp,
        "escape": escape,
        "size_image": size_image,
        "stride": (width + 3) & ~3,
        "clr_used": clr_used,
        "clr_important": clr_important,
        "palette": palette,
        "pixels": bytes(out),
        "complete": len(out) == need,
        "tail": tail,
        "tail_zero": all(t == 0 for t in tail),
        "consumed": i - start,
    }


def load(path):
    """Read one ANX file and decode every distinct record.

    Returns the frame count, the per-frame offset list, and a dict that maps
    each distinct offset to its decoded record.
    """
    with open(path, "rb") as fh:
        data = fh.read()
    return parse(data)


def parse(data):
    """Decode an ANX file already held in memory."""
    count = struct.unpack_from("<I", data, 0)[0]
    offsets = [struct.unpack_from("<I", data, 4 + 4 * i)[0] for i in range(count)]
    unique = sorted(set(offsets))
    bounds = [BASE + o for o in unique] + [len(data)]
    records = {}
    for k, off in enumerate(unique):
        records[off] = decode_record(data, BASE + off, bounds[k + 1])
    return count, offsets, records


def to_rgba(record, transparent_index=0):
    """Turn one decoded record into top-down RGBA bytes.

    Palette index `transparent_index` becomes alpha 0. Every other index becomes
    alpha 255.
    """
    pal = record["palette"]
    px = record["pixels"]
    width = record["width"]
    height = record["height"]
    table = []
    for i in range(len(pal) // 4):
        a = 0 if i == transparent_index else 255
        table.append(bytes((pal[i * 4 + 2], pal[i * 4 + 1], pal[i * 4], a)))
    if not table:
        table = [b"\x00\x00\x00\x00"]
    rows = []
    for y in range(height):
        row = px[y * width : (y + 1) * width]
        rows.append(b"".join(table[p] if p < len(table) else table[0] for p in row))
    rows.reverse()
    return rows


def to_rgb(record):
    """Turn one decoded record into top-down RGB bytes with no transparency."""
    pal = record["palette"]
    px = record["pixels"]
    width = record["width"]
    height = record["height"]
    table = [
        bytes((pal[i * 4 + 2], pal[i * 4 + 1], pal[i * 4]))
        for i in range(len(pal) // 4)
    ] or [b"\x00\x00\x00"]
    rows = []
    for y in range(height):
        row = px[y * width : (y + 1) * width]
        rows.append(b"".join(table[p] if p < len(table) else table[0] for p in row))
    rows.reverse()
    return rows

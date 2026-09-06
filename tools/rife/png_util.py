"""Minimal PNG writer and reader used by smoke.sh. No third-party modules."""
import struct
import zlib


def _chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff))


def write_rgb(path, width, height, pixels):
    """pixels is a bytes object of width*height*3 samples."""
    raw = b"".join(b"\x00" + pixels[y * width * 3:(y + 1) * width * 3]
                   for y in range(height))
    out = b"\x89PNG\r\n\x1a\n"
    out += _chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    out += _chunk(b"IDAT", zlib.compress(raw, 9))
    out += _chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(out)


def read_rgb(path):
    """Return (width, height, list of per-pixel red values)."""
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG: %s" % path
    pos, idat, hdr = 8, [], None
    while pos < len(data):
        length, tag = struct.unpack(">I", data[pos:pos + 4])[0], data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if tag == b"IHDR":
            hdr = struct.unpack(">IIBBBBB", body)
        elif tag == b"IDAT":
            idat.append(body)
        pos += 12 + length
    width, height, depth, color, _, _, interlace = hdr
    assert depth == 8 and interlace == 0, "unsupported PNG form in %s" % path
    channels = {0: 1, 2: 3, 4: 2, 6: 4}[color]
    raw = zlib.decompress(b"".join(idat))
    stride = width * channels
    prev, reds = bytearray(stride), []
    pos = 0
    for _ in range(height):
        filt = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if filt == 1:
                line[i] = (line[i] + a) & 0xff
            elif filt == 2:
                line[i] = (line[i] + b) & 0xff
            elif filt == 3:
                line[i] = (line[i] + (a + b) // 2) & 0xff
            elif filt == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xff
        reds.extend(line[i] for i in range(0, stride, channels))
        prev = line
    return width, height, reds


def centroid_x(width, height, reds, threshold=128):
    total = 0
    weighted = 0
    for y in range(height):
        row = y * width
        for x in range(width):
            if reds[row + x] >= threshold:
                total += 1
                weighted += x
    assert total > 0, "no bright pixels found"
    return weighted / total
